import { useState } from "react";
import api from "../api";

export default function DeviceProvisioning() {
    const [macAddress, setMacAddress] = useState("");
    const [slpt, setSlpt] = useState("");
    const [message, setMessage] = useState("");
    const [isError, setIsError] = useState(false);
    const [isLoading, setIsLoading] = useState(false);

    const handleProvisionRequest = async (e) => {
        e.preventDefault();
        setMessage("");
        setIsError(false);
        setSlpt("");

        if (!/^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$/.test(macAddress)) {
            setMessage("Please enter a valid MAC address (e.g., AA:BB:CC:DD:EE:FF).");
            setIsError(true);
            return;
        }

        setIsLoading(true);
        try {
            const response = await api.post("/devices/provision", {
                enrollment_id: macAddress.toUpperCase(),
            });
            setSlpt(response.data.slpt);
            setMessage("Provisioning token generated. Follow the steps below to activate the device.");
            setIsError(false);
        } catch (e) {
            const errMsg = e.response?.data?.msg || "An unexpected error occurred";
            setMessage(`Error: ${errMsg}`);
            setIsError(true);
        } finally {
            setIsLoading(false);
        }
    };

    return (
        <div className="provisioning-container">
            <form onSubmit={handleProvisionRequest}>
                <div className="form-group" style={{ maxWidth: '480px' }}>
                    <label htmlFor="macInput">Device Enrollment ID (MAC Address)</label>
                    <input
                        id="macInput"
                        type="text"
                        value={macAddress}
                        onChange={(e) => setMacAddress(e.target.value)}
                        placeholder="AA:BB:CC:DD:EE:FF"
                        required
                    />
                </div>

                <button
                    type="submit"
                    className="btn btn-primary"
                    disabled={isLoading}
                >
                    {isLoading ? "Generating…" : "Generate Provision Token"}
                </button>
            </form>

            {message && (
                <p className={isError ? "error-msg" : "success-msg"}>
                    {message}
                </p>
            )}

            {slpt && (
                <div className="provisioning-instructions">
                    <h3>Activation steps</h3>
                    <p>1. Connect to the device's Wi-Fi access point (e.g. <code className="text-mono">preSense_provision</code>).</p>
                    <p>2. Open <code className="text-mono">http://192.168.4.1/</code> in your browser.</p>
                    <p>3. Enter your factory Wi-Fi credentials.</p>
                    <p>4. Re-enter the same MAC address as the enrollment ID.</p>
                    <p>5. Paste the token below into the activation form:</p>
                    <code className="slpt-code">{slpt}</code>
                    <p className="text-sm text-faint">This token is single-use and expires in 10 minutes.</p>
                </div>
            )}
        </div>
    );
}
