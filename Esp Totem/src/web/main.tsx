import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { CustomerApp } from './customer/CustomerApp.tsx';
import { StaffApp } from './staff/StaffApp.tsx';
import './styles.css';

function Root() {
  const [, section, first] = window.location.pathname.split('/');
  if (section === 'm' && first) return <CustomerApp qrToken={first} />;
  if (section === 'painel' && first) return <StaffApp slug={first} />;

  return (
    <main className="center-page">
      <h1>Sistema de pedidos</h1>
      <p>Ambiente de demonstração com dados fictícios.</p>
      <a className="button primary" href="/painel/forno-demo">
        Abrir painel da equipe
      </a>
    </main>
  );
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <Root />
  </StrictMode>,
);
