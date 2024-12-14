// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Containers/Function.h"
#include "uLang/Common/Containers/Array.h"

namespace uLang
{
    using EventSubscriberId = uint32_t;

    /**
     * Registration portion of an TEvent<>. Split out as a standalone class, so 
     * systems can expose direct registration functionality without exposing 
     * execution rights, like so:
     *
     *      class CMySystem
     *      {
     *      public:
     *          using CMyEvent = TEvent<int>
     *          CMyEvent::Registrar& GetEvent() { return MyEvent; } // For external subscription
     *      private:
     *          CMyEvent MyEvent;
     */
    template <typename... ParamTypes>
    class TEventRegistrar
    {
    public:
        using FunctionType = uLang::TFunction<void(ParamTypes...)>;
        using SubscriberId = EventSubscriberId;

        template <
            typename FunctorType,
            typename = typename TEnableIf<
                TAnd<
                    TNot<TIsTFunction<typename TDecay<FunctorType>::Type>>,
                    Private::TFuncCanBindToFunctor<void(ParamTypes...), FunctorType>
                >::Value
            >::Type
        >
        SubscriberId Subscribe(FunctorType&& InFunc)
        {
            _Listeners.Emplace(SRegisteredListener{ _NextId, FunctionType(InFunc) });
            return _NextId++;
        }

        SubscriberId Subscribe(const FunctionType& Listener)
        {
            _Listeners.Emplace(SRegisteredListener{ _NextId, Listener });
            return _NextId++;
        }

        bool Unsubscribe(const SubscriberId ListenerId)
        {
            const int32_t FoundIndex = _Listeners.IndexOfByPredicate([ListenerId](const TEventRegistrar<ParamTypes ...>::SRegisteredListener& Listener)->bool
                {
                    return (Listener._Id == ListenerId);
                }
            );

            if (FoundIndex != uLang::IndexNone)
            {
                _Listeners.RemoveAtSwap(FoundIndex);
            }
            return (FoundIndex != uLang::IndexNone);
        }

        bool IsBound() const 
        {
            return uLang::IndexNone != _Listeners.IndexOfByPredicate([](const TEventRegistrar<ParamTypes ...>::SRegisteredListener& Listener)->bool
                {
                    return (bool)(Listener._Callback);
                }
            );
        }

        int32_t Num() const
        {
            return _Listeners.Num();
        }

        void Reset()
        {
            _Listeners.Empty();
        }

    private:
        TEventRegistrar() {}
        template <typename...> friend class TEvent;

    protected:
        SubscriberId _NextId = 0;
        struct SRegisteredListener
        {
            SubscriberId _Id;
            FunctionType _Callback;
        };
        TArray<SRegisteredListener> _Listeners;
    };

    /**
     * Generic event dispatcher. Declared using a comma separated parameter list 
     * to define its signature: 
     *      TEvent<int, float, bool> MyEvent;
     */
    template <typename... ParamTypes>
    class TEvent : public TEventRegistrar<ParamTypes...>
    {
    public:
        using Registrar = TEventRegistrar<ParamTypes...>;

        void Broadcast(ParamTypes... Params)
        {
            for (const typename Registrar::SRegisteredListener& Listener : Registrar::_Listeners)
            {
                if (Listener._Callback)
                {
                    Listener._Callback(Params...);
                }
            }
        }
    };

} // namespace uLang