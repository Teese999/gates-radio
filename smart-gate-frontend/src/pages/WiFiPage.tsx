import React, { useState, useEffect } from 'react';
import './WiFiPage.css';

interface WiFiNetwork {
  ssid: string;
  rssi: number;
  encryption: number;
}

interface WiFiPageProps {
  onBack: () => void;
  apiCall: (endpoint: string, method?: string, data?: any) => Promise<any>;
  addLog: (message: string, type?: 'info' | 'error' | 'success' | 'warning') => void;
}

function WiFiPage({ onBack, apiCall, addLog }: WiFiPageProps) {
  const [networks, setNetworks] = useState<WiFiNetwork[]>([]);
  const [scanning, setScanning] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [selectedNetwork, setSelectedNetwork] = useState<WiFiNetwork | null>(null);
  const [password, setPassword] = useState('');
  const [showPasswordInput, setShowPasswordInput] = useState(false);

  // Сканирование сетей при загрузке
  useEffect(() => {
    scanNetworks();
  }, []);

  const scanNetworks = async () => {
    setScanning(true);
    addLog('🔍 Сканирование WiFi сетей...', 'info');
    
    try {
      const result = await apiCall('/api/wifi/scan');
      setNetworks(result);
      addLog(`📡 Найдено сетей: ${result.length}`, 'success');
    } catch (error) {
      addLog('❌ Ошибка сканирования', 'error');
    } finally {
      setScanning(false);
    }
  };

  const handleNetworkClick = (network: WiFiNetwork) => {
    setSelectedNetwork(network);
    if (network.encryption !== 0) {
      setShowPasswordInput(true);
      setPassword('');
    } else {
      // Открытая сеть, подключаемся сразу
      connectToNetwork(network, '');
    }
  };

  const connectToNetwork = async (network: WiFiNetwork, pass: string) => {
    setConnecting(true);
    addLog(`🔌 Подключение к ${network.ssid}...`, 'info');
    
    try {
      const result = await apiCall('/api/wifi/connect', 'POST', {
        ssid: network.ssid,
        password: pass
      });
      
      if (result.success) {
        addLog(`✅ Подключено! IP: ${result.ip}`, 'success');
        setShowPasswordInput(false);
        setPassword('');
        
        // Возвращаемся на главную через 2 секунды
        setTimeout(() => {
          onBack();
        }, 2000);
      } else {
        addLog(`❌ Ошибка подключения: ${result.error}`, 'error');
      }
    } catch (error) {
      addLog('❌ Ошибка подключения', 'error');
    } finally {
      setConnecting(false);
    }
  };

  const handleConnect = () => {
    if (selectedNetwork) {
      connectToNetwork(selectedNetwork, password);
    }
  };

  const getSignalIcon = (rssi: number) => {
    if (rssi > -50) return '📶';
    if (rssi > -70) return '📶';
    return '📶';
  };

  const getSignalClass = (rssi: number) => {
    if (rssi > -50) return 'signal-strong';
    if (rssi > -70) return 'signal-medium';
    return 'signal-weak';
  };

  return (
    <div className="page-container">
      <div className="page-header">
        <button className="btn-back" onClick={onBack}>← Назад</button>
        <h2>📡 Управление WiFi</h2>
        <button 
          className="btn btn-primary" 
          onClick={scanNetworks}
          disabled={scanning}
        >
          {scanning ? '⏳ Сканирование...' : '🔄 Обновить'}
        </button>
      </div>

      {showPasswordInput && selectedNetwork && (
        <div className="password-modal">
          <div className="password-modal-content">
            <h3>🔐 Подключение к {selectedNetwork.ssid}</h3>
            <input
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              onKeyPress={(e) => {
                if (e.key === 'Enter' && password && !connecting) {
                  handleConnect();
                }
              }}
              placeholder="Введите пароль"
              className="password-input"
              disabled={connecting}
              autoFocus
            />
            <div className="password-buttons">
              <button 
                className="btn btn-success" 
                onClick={handleConnect}
                disabled={connecting || !password}
              >
                {connecting ? '⏳ Подключение...' : '✅ Подключиться'}
              </button>
              <button 
                className="btn btn-danger" 
                onClick={() => {
                  setShowPasswordInput(false);
                  setPassword('');
                }}
                disabled={connecting}
              >
                ❌ Отмена
              </button>
            </div>
          </div>
        </div>
      )}

      <div className="networks-list">
        {networks.length === 0 && !scanning && (
          <div className="empty-state">
            <p>Нет доступных сетей</p>
            <p>Нажмите "Обновить" для сканирования</p>
          </div>
        )}

        {networks.map((network, index) => (
          <div 
            key={index} 
            className={`network-item ${getSignalClass(network.rssi)}`}
            onClick={() => handleNetworkClick(network)}
          >
            <div className="network-icon">
              {getSignalIcon(network.rssi)}
            </div>
            <div className="network-info">
              <div className="network-ssid">{network.ssid}</div>
              <div className="network-details">
                <span className="network-rssi">{network.rssi} dBm</span>
                <span className="network-security">
                  {network.encryption === 0 ? '🔓 Открытая' : '🔒 Защищённая'}
                </span>
              </div>
            </div>
            <div className="network-arrow">→</div>
          </div>
        ))}
      </div>
    </div>
  );
}

export default WiFiPage;
