import { Outlet, Link, useNavigate } from 'react-router-dom';
import { useAuth } from './utilities/AuthProvider';
import './App.css';

export default function App() {
  const { user, logout } = useAuth();
  const nav = useNavigate();

  function onLogout() {
    logout();
    nav('/login');
  }

  return (
    <div className="app-shell">
      {user && (
        <nav className="app-nav">
          <Link to="/dashboard" className="app-nav__brand">Stitch Intelligence</Link>

          <div className="app-nav__menu">
            <span className="app-nav__user">
              <span className="status-dot status-dot--online" aria-hidden="true"></span>
              <strong>{user.username}</strong>
              <span className="text-faint">·</span>
              <span>{user.organization}</span>
            </span>
            <button onClick={onLogout} className="btn btn-ghost btn-sm">
              Logout
            </button>
          </div>
        </nav>
      )}

      <main className="app-main">
        <Outlet />
      </main>
    </div>
  );
}
