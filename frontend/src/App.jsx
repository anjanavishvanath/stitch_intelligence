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
    <div>
      {user ? <nav>
        <Link to='/dashboard'>Stitch Intelligence</Link>
        <div>
          <Link to='/logout' onClick={onLogout}>Logout</Link>          
        </div>
        <div>
          <span>{user.username} | {user.organization}</span>
        </div>
      </nav>: null}
      <main><Outlet /></main>
    </div>
  )
}