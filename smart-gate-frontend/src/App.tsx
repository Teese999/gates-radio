import React, { useState, useEffect } from 'react';
import './App.css';
import WiFiPage from './pages/WiFiPage';
import PhonePage from './pages/PhonePage';
import KeyPage from './pages/KeyPage';
import SettingsPage from './pages/SettingsPage';

// WebSocket подключение
const WS_URL = window.location.hostname === 'smartgate.local' 
  ? 'ws://smartgate.local:81' 
  : 'ws://192.168.4.1:81';

interface KeyData {
  key: number;
  bitLength: number;
  protocol: number;
  timestamp: number;
}

interface LogEntry {
  timestamp: number;
  message: string;
  type: 'info' | 'error' | 'success' | 'warning';
}

function App() {
  const [ws, setWs] = useState<WebSocket | null>(null);
  const [connected, setConnected] = useState(false);
  const [wifiStatus, setWifiStatus] = useState('disconnected');
  const [wifiInfo, setWifiInfo] = useState<{ssid: string, rssi: number, ip: string} | null>(null);
  const [phoneCount, setPhoneCount] = useState(0);
  const [keyCount, setKeyCount] = useState(0);
  const [recentKeys, setRecentKeys] = useState<KeyData[]>([]);
  const [notification, setNotification] = useState<string | null>(null);
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [currentPage, setCurrentPage] = useState<'home' | 'wifi' | 'phones' | 'keys' | 'settings'>('home');

  // Добавление лога
  const addLog = (message: string, type: 'info' | 'error' | 'success' | 'warning' = 'info') => {
    const log: LogEntry = {
      timestamp: Date.now(),
      message,
      type
    };
    setLogs(prev => [log, ...prev.slice(0, 99)]); // Храним последние 100 логов
  };

  // Обработка WebSocket сообщений
  const handleWebSocketMessage = (data: any) => {
    switch (data.event) {
      case 'log':
        addLog(data.data.message, data.data.type || 'info');
        break;
      case 'key_received':
        setRecentKeys(prev => [data.data, ...prev.slice(0, 9)]);
        setNotification(`Получен ключ: ${data.data.key}`);
        addLog(`🔑 Получен ключ: ${data.data.key} (протокол: ${data.data.protocol})`, 'info');
        break;
      case 'key_added':
        setKeyCount(prev => prev + 1);
        setNotification(`Добавлен новый ключ: ${data.data.name}`);
        addLog(`🔑 Добавлен новый ключ: ${data.data.name}`, 'success');
        break;
      case 'wifi_status':
        console.log('WiFi Status Update:', data.data);
        setWifiStatus(data.data.status);
        if (data.data.status === 'connected' && data.data.ssid) {
          setWifiInfo({
            ssid: data.data.ssid,
            rssi: data.data.rssi,
            ip: data.data.ip
          });
        } else {
          setWifiInfo(null);
        }
        break;
      case 'phone_count':
        setPhoneCount(data.data.count);
        break;
      case 'key_count':
        setKeyCount(data.data.count);
        break;
      default:
        console.log('Неизвестное событие:', data.event);
    }
  };

  // WebSocket подключение
  useEffect(() => {
    let websocket: WebSocket | null = null;
    let reconnectTimeout: NodeJS.Timeout | null = null;
    
    const connect = () => {
      websocket = new WebSocket(WS_URL);
      
      websocket.onopen = () => {
        console.log('WebSocket подключен');
        setConnected(true);
        setWs(websocket);
        addLog('✅ WebSocket подключен', 'success');
      };
      
      websocket.onclose = () => {
        console.log('WebSocket отключен');
        setConnected(false);
        setWs(null);
        addLog('❌ WebSocket отключен', 'error');
        
        // Переподключение через 3 секунды
        reconnectTimeout = setTimeout(() => {
          addLog('🔄 Переподключение...', 'warning');
          connect();
        }, 3000);
      };
      
      websocket.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          handleWebSocketMessage(data);
        } catch (error) {
          console.error('Ошибка парсинга WebSocket сообщения:', error);
        }
      };
      
      websocket.onerror = (error) => {
        console.error('WebSocket ошибка:', error);
      };
    };
    
    connect();
    
    return () => {
      if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
      }
      if (websocket) {
        websocket.close();
      }
    };
  }, []);

  // API вызовы
  const apiCall = async (endpoint: string, method: string = 'GET', data: any = null) => {
    const baseUrl = window.location.hostname === 'smartgate.local' 
      ? 'http://smartgate.local' 
      : 'http://192.168.4.1';
    
    const response = await fetch(`${baseUrl}${endpoint}`, {
      method,
      headers: {
        'Content-Type': 'application/json',
      },
      body: data ? JSON.stringify(data) : null,
    });
    
    return response.json();
  };

  // Управление воротами
  const [gateTriggering, setGateTriggering] = useState(false);
  
  const triggerGate = async () => {
    if (gateTriggering) return; // Предотвращаем двойной клик
    
    setGateTriggering(true);
    try {
      await apiCall('/api/gate/trigger', 'POST');
      setNotification('Сигнал отправлен');
    } catch (error) {
      setNotification('Ошибка отправки сигнала');
      addLog('❌ Ошибка отправки сигнала', 'error');
    } finally {
      // Разрешаем следующий клик через 1 секунду
      setTimeout(() => {
        setGateTriggering(false);
      }, 1000);
    }
  };

  // Загрузка статистики
  useEffect(() => {
    const loadStats = async () => {
      try {
        const phones = await apiCall('/api/phones');
        const keys = await apiCall('/api/keys');
        setPhoneCount(phones.length);
        setKeyCount(keys.length);
      } catch (error) {
        console.error('Ошибка загрузки статистики:', error);
      }
    };
    
    loadStats();
  }, []);

  // Рендер страниц
  if (currentPage === 'wifi') {
    return (
      <div className="App">
        <div className="container">
          <WiFiPage 
            onBack={() => setCurrentPage('home')}
            apiCall={apiCall}
            addLog={addLog}
          />
        </div>
      </div>
    );
  }

  if (currentPage === 'phones') {
    return (
      <div className="App">
        <div className="container">
          <PhonePage 
            onBack={() => setCurrentPage('home')}
            apiCall={apiCall}
            addLog={addLog}
          />
        </div>
      </div>
    );
  }

  if (currentPage === 'keys') {
    return (
      <div className="App">
        <div className="container">
          <KeyPage 
            onBack={() => setCurrentPage('home')}
            apiCall={apiCall}
            addLog={addLog}
          />
        </div>
      </div>
    );
  }

  if (currentPage === 'settings') {
    return (
      <div className="App">
        <div className="container">
          <SettingsPage 
            onBack={() => setCurrentPage('home')}
            apiCall={apiCall}
            addLog={addLog}
          />
        </div>
      </div>
    );
  }

  return (
    <div className="App">
      <div className="container">
        {/* Заголовок */}
        <div className="header">
          <h1>🏠 Умные Ворота</h1>
          <div className={`ws-status ${connected ? 'connected' : 'disconnected'}`}>
            WebSocket: {connected ? 'Подключен' : 'Отключен'}
          </div>
        </div>

        {/* Статистика */}
        <div className="stats">
          <div className="stat-card clickable wifi-card" onClick={() => setCurrentPage('wifi')}>
            <div className="stat-icon">📶</div>
            {wifiStatus === 'connected' && wifiInfo ? (
              <>
                <div className="stat-number">✅</div>
                <div className="stat-label">{wifiInfo.ssid}</div>
                <div className="wifi-details">
                  <span className="wifi-rssi">{wifiInfo.rssi} dBm</span>
                  <span className="wifi-ip">{wifiInfo.ip}</span>
                </div>
              </>
            ) : (
              <>
                <div className="stat-number">❌</div>
                <div className="stat-label">WiFi отключен</div>
              </>
            )}
          </div>
          
          <div className="stat-card clickable" onClick={() => setCurrentPage('phones')}>
            <div className="stat-icon">📱</div>
            <div className="stat-number">{phoneCount}</div>
            <div className="stat-label">Телефоны</div>
          </div>
          
          <div className="stat-card clickable" onClick={() => setCurrentPage('keys')}>
            <div className="stat-icon">🔑</div>
            <div className="stat-number">{keyCount}</div>
            <div className="stat-label">Ключи 433MHz</div>
          </div>
          
          <div className="stat-card clickable" onClick={() => setCurrentPage('settings')}>
            <div className="stat-icon">⚙️</div>
            <div className="stat-number">—</div>
            <div className="stat-label">Настройки</div>
          </div>
        </div>

        {/* Управление воротами */}
        <div className="gate-control">
          <h2>🚪 Управление воротами</h2>
          <div className="button-group">
            <button 
              className="btn btn-gate" 
              onClick={triggerGate}
              disabled={gateTriggering}
            >
              {gateTriggering ? '⏳ Отправка...' : '⚡ Активировать ворота'}
            </button>
          </div>
        </div>

        {/* Последние ключи */}
        {recentKeys.length > 0 && (
          <div className="recent-keys">
            <h3>🔑 Последние полученные ключи</h3>
            <div className="key-list">
              {recentKeys.map((key, index) => (
                <div key={index} className="key-item">
                  <div className="key-code">
                    <strong>Ключ:</strong> {key.key}
                  </div>
                  <div className="key-time">
                    <strong>Время:</strong> {new Date(key.timestamp).toLocaleTimeString()}
                  </div>
                  <div className="key-details">
                    <small>Протокол: {key.protocol}, Биты: {key.bitLength}</small>
                  </div>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* Консоль логов */}
        <div className="console">
          <div className="console-header">
            <h3>📟 Консоль логов</h3>
            <button className="btn-clear" onClick={() => setLogs([])}>Очистить</button>
          </div>
          <div className="console-body">
            {logs.length === 0 ? (
              <div className="console-empty">Логи отсутствуют</div>
            ) : (
              logs.map((log, index) => (
                <div key={index} className={`console-line console-${log.type}`}>
                  <span className="console-time">
                    {new Date(log.timestamp).toLocaleTimeString()}
                  </span>
                  <span className="console-message">{log.message}</span>
                </div>
              ))
            )}
          </div>
        </div>

        {/* Уведомления */}
        {notification && (
          <div className="notification" onClick={() => setNotification(null)}>
            {notification}
          </div>
        )}
      </div>
    </div>
  );
}

export default App;