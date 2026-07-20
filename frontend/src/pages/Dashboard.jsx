import { useAuth } from "../utilities/AuthProvider";
import { Link } from "react-router-dom";

export default function Dashboard() {
    const { user } = useAuth();

    return (
        <div className="page">
            <header className="page-header">
                <div>
                    <h1 className="page-title">Dashboard</h1>
                    <p className="page-subtitle">
                        Welcome back, <span className="text-highlight">{user.username}</span>
                        <span className="text-faint"> · </span>
                        {user.organization}
                    </p>
                </div>
            </header>

            <div className="grid-view">
                {/* Active feature */}
                <article className="card card-hoverable">
                    <div className="card-header">
                        <h3 className="card-title">Sensor Registry</h3>
                        <span className="badge badge-info">IoT</span>
                    </div>
                    <div className="card-body">
                        <p>Provision new sewing-machine nodes and review the active fleet.</p>
                    </div>
                    <div className="card-actions">
                        <Link to="/sensor_registry" className="btn btn-primary">
                            View Sensors
                        </Link>
                    </div>
                </article>

                {/* Placeholders for upcoming features — gives the grid a settled rhythm */}
                <article className="card" style={{ opacity: 0.55 }}>
                    <div className="card-header">
                        <h3 className="card-title">Production Lines</h3>
                        <span className="badge">Coming soon</span>
                    </div>
                    <div className="card-body">
                        <p>Configure lines and assign sewing machines to stations.</p>
                    </div>
                </article>

                <article className="card" style={{ opacity: 0.55 }}>
                    <div className="card-header">
                        <h3 className="card-title">Analytics</h3>
                        <span className="badge">Coming soon</span>
                    </div>
                    <div className="card-body">
                        <p>Cycle time, throughput, and bottleneck detection per station.</p>
                    </div>
                </article>

                <article className="card" style={{ opacity: 0.55 }}>
                    <div className="card-header">
                        <h3 className="card-title">Live Floor</h3>
                        <span className="badge">Coming soon</span>
                    </div>
                    <div className="card-body">
                        <p>Real-time view of every station — pulses, gaps, and alerts.</p>
                    </div>
                </article>
            </div>
        </div>
    );
}
