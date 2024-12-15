
""" """
def constant(f):
    def fset(self, value):
        raise TypeError
    def fget(self):
        return f()
    return property(fget, fset)

""" """
class _Logger_Defs:
    _with_ue = None

    @constant
    def WITH_UE():
        if _Logger_Defs._with_ue is not None:
            return _Logger_Defs._with_ue
        
        try:
            import unreal
        except ImportError:
            _Logger_Defs._with_ue = False
        else:
            _Logger_Defs._with_ue = True

        return _Logger_Defs._with_ue

DEFS = _Logger_Defs()