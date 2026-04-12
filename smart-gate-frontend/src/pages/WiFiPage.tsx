import React, { useState, useEffect } from 'react';
import { useApp } from '../App';

interface WiFiNetwork {
  ssid: string;
  rssi: number;
  encryption: number;
}

const WiFiPage: React.FC = () => {
  const { apiCall, addLog } = useApp();
  const [networks, setNetworks] = useState<WiFiNetwork[]>([]);
  const [scanning, setScanning] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [selected, setSelected] = useState<WiFiNetwork | null>(null);
  const [password, setPassword] = useState('');

  const scan = async () => {
    setScanning(true);
    try {
      const result = await apiCall('/api/wifi/scan');
      if (Array.isArray(result)) {
        setNetworks(result.sort((a, b) => b.rssi - a.rssi));
        addLog(`Найдено сетей: ${result.length}`, 'success');
      }
    } catch {
      addLog('Ошибка сканирования', 'error');
    } finally {
      setScanning(false);
    }
  };

  useEffect(() => { scan(); }, []);

  const connect = async () => {
    if (!selected) return;
    setConnecting(true);
    addLog(`Подключение к ${selected.ssid}...`, 'info');
    try {
      const result = await apiCall('/api/wifi/connect', 'POST', {
        ssid: selected.ssid,
        password,
      });
      if (result.success) {
        addLog(`Подключено! IP: ${result.ip}`, 'success');
        setSelected(null);
        setPassword('');
      } else {
        addLog(`Ошибка: ${result.error}`, 'error');
      }
    } catch {
      addLog('Ошибка подключения', 'error');
    } finally {
      setConnecting(false);
    }
  };

  const handleNetworkClick = (net: WiFiNetwork) => {
    if (net.encryption === 0) {
      setSelected(net);
      setPassword('');
      // Connect immediately to open networks
      setConnecting(true);
      apiCall('/api/wifi/connect', 'POST', { ssid: net.ssid, password: '' })
        .then(r => {
          if (r.success) addLog(`Подключено! IP: ${r.ip}`, 'success');
          else addLog(`Ошибка: ${r.error}`, 'error');
        })
        .catch(() => addLog('Ошибка подключения', 'error'))
        .finally(() => { setConnecting(false); setSelected(null); });
    } else {
      setSelected(net);
      setPassword('');
    }
  };

  const signalLevel = (rssi: number) => {
    if (rssi > -50) return 'green';
    if (rssi > -70) return 'orange';
    return 'red';
  };

  return (
    <div>
      <div className="page-title">WiFi</div>

      <button
        className="btn btn--primary btn--full"
        onClick={scan}
        disabled={scanning}
        style={{ marginBottom: 12 }}
      >
        {scanning ? 'Сканирование...' : 'Сканировать сети'}
      </button>

      {networks.length === 0 && !scanning ? (
        <div className="section">
          <div className="empty">
            <div className="empty-title">Сети не найдены</div>
            <div className="empty-sub">Нажмите «Сканировать» для поиска</div>
          </div>
        </div>
      ) : (
        <div className="section">
          <div className="section-header">Доступные сети ({networks.length})</div>
          {networks.map((net, i) => (
            <div
              key={i}
              className="list-item"
              onClick={() => handleNetworkClick(net)}
              style={{ cursor: 'pointer' }}
            >
              <div className="list-item-body">
                <div className="list-item-title">{net.ssid || '(скрытая сеть)'}</div>
                <div className="list-item-sub">
                  <span className={`badge badge--${signalLevel(net.rssi)}`}>{net.rssi} dBm</span>
                  {' '}
                  <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>
                    {net.encryption === 0 ? 'Открытая' : 'Защищённая'}
                  </span>
                </div>
              </div>
              <span style={{ color: 'var(--text-muted)', fontSize: 18 }}>›</span>
            </div>
          ))}
        </div>
      )}

      {/* Password modal */}
      {selected && selected.encryption !== 0 && (
        <div className="modal-overlay" onClick={() => setSelected(null)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <div className="modal-title">{selected.ssid}</div>
            <div className="modal-text">Введите пароль для подключения</div>
            <input
              className="input"
              type="password"
              value={password}
              onChange={e => setPassword(e.target.value)}
              onKeyDown={e => e.key === 'Enter' && password && !connecting && connect()}
              placeholder="Пароль"
              autoFocus
              disabled={connecting}
              style={{ marginBottom: 16 }}
            />
            <div className="modal-actions">
              <button className="btn btn--ghost" onClick={() => setSelected(null)} disabled={connecting}>
                Отмена
              </button>
              <button className="btn btn--primary" onClick={connect} disabled={!password || connecting}>
                {connecting ? 'Подключение...' : 'Подключить'}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default WiFiPage;
