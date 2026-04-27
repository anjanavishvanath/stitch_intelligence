import { useAuth } from "../utilities/AuthProvider";
import { useState, useEffect } from "react";
import { Link } from "react-router-dom";
import DeviceProvisioning from "../components/DeviceProvisioning";
import api from "../api";


export default function SensorsPage() {
    const user = useAuth()
    const [showForm, setShowForm] = useState(false)
    const [sensors, setSensors] = useState([])

    const fetchSensors = async () => {
        try {
            const responseSensors = await api.get('/devices/by_user')
            console.log("Sensors fetched:", responseSensors.data.devices);
            setSensors(responseSensors.data.devices);
        } catch (error) {
            console.log("Error fetching sensors:", error);
        }
    }

    useEffect(() => {
        fetchSensors();
    }, [])

    const sensorList = sensors.map((sensor) => (
        <div key={sensor.id} className="sensor-card">
            <h3>{sensor.device_name ? sensor.device_name : "Unnamed Device"}</h3>
            <p className="text-muted">ID: {sensor.id}</p>
            <p><strong>MAC Address:</strong> {sensor.enrollment_id}</p>
            <p><span className="text-highlight">Created:</span> {new Date(sensor.created_at).toLocaleDateString()}</p>
            <p><span className="text-highlight">Asset ID:</span> {sensor.asset_id || 'Unassigned'}</p>
        </div>
    ))

    return (
        <div>
            <div>
                <h1>Sensor Registry</h1>
                <Link to="/dashboard" className="btn btn-secondary">Back to Dashboard</Link>
            </div>
            <button className="btn btn-primary" onClick={() => setShowForm(prevState => !prevState)}>
                {showForm ? 'Cancel' : 'Provision New Device'}
            </button>

            {showForm && (
                <div>
                    <h2>Get Activation Code</h2>
                    <DeviceProvisioning />
                </div>
            )}

            <div className="grid-view">{sensorList}</div>

        </div>
    )
}
