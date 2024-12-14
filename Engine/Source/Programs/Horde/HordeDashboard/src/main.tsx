import React from 'react'
import ReactDOM from 'react-dom/client'
import App from './App.tsx'
import './index.css'
import { configure } from 'mobx';

// configure mobx
configure({
   reactionRequiresObservable: true,
   enforceActions: "observed"
});

ReactDOM.createRoot(document.getElementById('root')!).render(<App />);

