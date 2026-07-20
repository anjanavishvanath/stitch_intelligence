import { useState, useEffect, useRef } from "react";
import { Link } from "react-router-dom";
import DeviceProvisioning from "../components/DeviceProvisioning";
import api from "../api";

const REFRESH_MS   = 3000;   // poll cadence — pieces are seconds apart, not ms
const PIECE_LIMIT  = 15;     // how many recent pieces per waveform strip

export default function SensorsPage() {
    const [showForm, setShowForm] = useState(false);
    const [summary,  setSummary]  = useState(null);
    const [devices,  setDevices]  = useState([]);
    const [pieces,   setPieces]   = useState({});    // device.id -> pieces[]
    const [loading,  setLoading]  = useState(true);
    const pollRef = useRef(null);

    const refresh = async () => {
        try {
            const [sRes, dRes] = await Promise.all([
                api.get("/summary"),
                api.get("/devices/by_user"),
            ]);
            setSummary(sRes.data);
            const list = dRes.data.devices ?? [];
            setDevices(list);
            // Fan out per-device piece fetches. Non-critical if any single one fails.
            await Promise.all(list.map(async (d) => {
                try {
                    const pRes = await api.get(`/devices/${d.id}/pieces?limit=${PIECE_LIMIT}`);
                    setPieces(prev => ({ ...prev, [d.id]: pRes.data.pieces ?? [] }));
                } catch (_) {}
            }));
        } catch (e) {
            console.log("refresh failed:", e);
        } finally {
            setLoading(false);
        }
    };

    useEffect(() => {
        refresh();
        pollRef.current = setInterval(refresh, REFRESH_MS);
        return () => clearInterval(pollRef.current);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    return (
        <div className="page">
            <header className="page-header">
                <div>
                    <h1 className="page-title">Line Overview</h1>
                    <p className="page-subtitle">
                        {summary?.organization ?? "Loading…"} · live shop-floor productivity
                    </p>
                </div>
                <div className="page-actions">
                    <Link to="/dashboard" className="btn btn-ghost btn-sm">← Dashboard</Link>
                    <button
                        type="button"
                        className={showForm ? "btn btn-secondary" : "btn btn-primary"}
                        onClick={() => setShowForm(prev => !prev)}
                    >
                        {showForm ? "Cancel" : "+ Provision Device"}
                    </button>
                </div>
            </header>

            {showForm && (
                <section className="section">
                    <h2 className="section-title">Get Activation Code</h2>
                    <DeviceProvisioning />
                </section>
            )}

            <SummaryStrip summary={summary} />

            <section className="section">
                <h2 className="section-title">
                    Machines
                    {!loading && (
                        <span className="badge" style={{ marginLeft: "var(--space-3)" }}>
                            {devices.length}
                        </span>
                    )}
                </h2>

                <div className="grid-view">
                    {loading ? (
                        <div className="empty-state"><p>Loading…</p></div>
                    ) : devices.length === 0 ? (
                        <div className="empty-state">
                            <h3 className="empty-state__title">No machines yet</h3>
                            <p>Provision your first device to start capturing pieces.</p>
                        </div>
                    ) : (
                        devices.map((d) => (
                            <MachineCard
                                key={d.id}
                                device={d}
                                pieces={pieces[d.id] ?? []}
                            />
                        ))
                    )}
                </div>
            </section>
        </div>
    );
}


/* ============================================================
 * Top summary strip
 * ============================================================ */
function SummaryStrip({ summary }) {
    if (!summary) return null;

    const stitching = summary.stitching_ratio_pct ?? 0;

    return (
        <section className="summary-strip">
            <SummaryStat
                label="Active machines"
                value={`${summary.active_devices} / ${summary.total_devices}`}
                sub={`${summary.idle_devices} idle`}
                accent={summary.active_devices > 0 ? "success" : "muted"}
            />
            <SummaryStat
                label="Pieces today"
                value={summary.pieces_today?.toLocaleString() ?? "0"}
                sub="across the line"
                accent="info"
            />
            <SummaryStat
                label="Stitching ratio"
                value={`${stitching.toFixed(1)}%`}
                sub="stitching time / cycle time"
                accent={stitching > 50 ? "success" : stitching > 25 ? "warning" : "error"}
            />
            <SummaryStat
                label="Avg trim + wipe"
                value={`${Math.round(summary.avg_trim_wipe_ms ?? 0)} ms`}
                sub="per piece"
                accent="muted"
            />
        </section>
    );
}

function SummaryStat({ label, value, sub, accent = "muted" }) {
    return (
        <div className={`summary-stat summary-stat--${accent}`}>
            <div className="summary-stat__label">{label}</div>
            <div className="summary-stat__value">{value}</div>
            <div className="summary-stat__sub">{sub}</div>
        </div>
    );
}


/* ============================================================
 * Per-machine card
 * ============================================================ */
function MachineCard({ device, pieces }) {
    const roll   = device.rollup ?? {};
    const latest = device.latest_piece;
    const online = isFresh(roll.last_piece_at);

    const stitchRatio = roll.avg_stitching_ratio != null
        ? (roll.avg_stitching_ratio * 100).toFixed(1) + "%"
        : "—";

    return (
        <article className="sensor-card">
            <div className="sensor-card__head">
                <h3>{device.device_name || `Machine #${device.id}`}</h3>
                <span className={`badge ${online ? "badge-success" : ""}`}>
                    <span className={`status-dot ${online ? "status-dot--online" : "status-dot--offline"}`} aria-hidden="true"></span>
                    {online ? "Active" : "Idle"}
                </span>
            </div>

            <p className="text-sm text-mono text-muted" style={{ marginTop: "calc(-1 * var(--space-1))" }}>
                {device.enrollment_id}
            </p>

            {/* Today's rollup */}
            <div className="stat-grid">
                <Stat  label="Pieces today"     value={roll.pieces_today ?? 0} />
                <Stat  label="Avg cycle"        value={fmtMs(roll.avg_cycle_time_ms)} />
                <Stat  label="Stitching ratio"  value={stitchRatio} />
                <Stat  label="Micro-stops"      value={roll.total_adjustments_today ?? 0} />
            </div>

            {/* Waveform of recent pieces */}
            <div className="waveform">
                <div className="waveform__label">
                    Last {Math.min(pieces.length, PIECE_LIMIT)} pieces
                </div>
                <PieceStrip pieces={pieces} />
            </div>

            {/* Latest piece footer */}
            {latest ? (
                <p className="text-xs text-faint" style={{ marginTop: "var(--space-3)" }}>
                    Last piece: {fmtMs(latest.total_cycle_time_ms)} · {latest.adjustment_count} adj · {latest.status}
                </p>
            ) : (
                <p className="text-xs text-faint" style={{ marginTop: "var(--space-3)" }}>
                    Awaiting first piece…
                </p>
            )}
        </article>
    );
}


/* ============================================================
 * Piece waveform — a row of vertical columns, each = one piece
 * Segments colored: stitching (green) at bottom, adjustment (amber) middle,
 * trim+wipe (gray) at top. Heights proportional to durations.
 * ============================================================ */
function PieceStrip({ pieces }) {
    if (!pieces || pieces.length === 0) {
        return <div className="waveform__placeholder">No pieces yet…</div>;
    }
    return (
        <div className="waveform__strip">
            {pieces.map((p, i) => (
                <PieceColumn key={p.piece_seq ?? i} piece={p} />
            ))}
        </div>
    );
}

function PieceColumn({ piece }) {
    const total  = Math.max(piece.total_cycle_time_ms ?? 1, 1);
    const stitch = piece.total_stitching_ms  ?? 0;
    const adjust = piece.total_adjustment_ms ?? 0;
    const trim   = piece.trim_and_wipe_time_ms ?? 0;

    // Percentages (may not sum to 100 if the state machine had transient gaps)
    const stitchPct = Math.min(100, (stitch / total) * 100);
    const adjustPct = Math.min(100, (adjust / total) * 100);
    const trimPct   = Math.min(100, (trim   / total) * 100);

    const title =
        `Piece #${piece.piece_seq} · ${piece.status}\n` +
        `Cycle: ${fmtMs(piece.total_cycle_time_ms)}\n` +
        `Stitching: ${fmtMs(stitch)} (${stitchPct.toFixed(0)}%)\n` +
        `Adjusting: ${fmtMs(adjust)} (${adjustPct.toFixed(0)}%)\n` +
        `Trim+Wipe: ${fmtMs(trim)}\n` +
        `Stitches: ${piece.total_stitches}`;

    return (
        <div className="piece-col" title={title}>
            <div className="piece-col__trim"   style={{ height: `${trimPct}%`   }}></div>
            <div className="piece-col__adjust" style={{ height: `${adjustPct}%` }}></div>
            <div className="piece-col__stitch" style={{ height: `${stitchPct}%` }}></div>
        </div>
    );
}


/* ============================================================
 * Little bits
 * ============================================================ */
function Stat({ label, value }) {
    return (
        <div className="stat">
            <div className="stat__value">{value}</div>
            <div className="stat__label">{label}</div>
        </div>
    );
}

function fmtMs(v) {
    if (v == null) return "—";
    if (v < 1000)  return `${Math.round(v)} ms`;
    return `${(v / 1000).toFixed(1)} s`;
}

function isFresh(iso) {
    if (!iso) return false;
    return Date.now() - new Date(iso).getTime() < 30_000;   // 30 s
}
