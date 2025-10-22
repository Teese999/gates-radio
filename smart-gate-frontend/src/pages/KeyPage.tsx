import React, { useState, useEffect } from 'react';
import './KeyPage.css';

interface Key {
  code: number;
  name: string;
  enabled: boolean;
  bitLength: number;
  protocol: number;
  timestamp: number;
}

interface KeyPageProps {
  onBack: () => void;
  apiCall: (endpoint: string, method?: string, data?: any) => Promise<any>;
  addLog: (message: string, type?: 'info' | 'error' | 'success' | 'warning') => void;
}

const KeyPage: React.FC<KeyPageProps> = ({ onBack, apiCall, addLog }) => {
  const [keys, setKeys] = useState<Key[]>([]);
  const [loading, setLoading] = useState(false);
  const [learning, setLearning] = useState(false);
  const [showDeleteConfirm, setShowDeleteConfirm] = useState<number | null>(null);
  const [editingKey, setEditingKey] = useState<number | null>(null);
  const [editingName, setEditingName] = useState('');

  // Загрузка списка ключей
  const loadKeys = async () => {
    try {
      setLoading(true);
      const response = await apiCall('/api/keys');
      setKeys(response);
    } catch (error) {
      addLog('❌ Ошибка загрузки ключей', 'error');
      console.error('Ошибка загрузки ключей:', error);
    } finally {
      setLoading(false);
    }
  };

  // Проверка статуса режима обучения
  const checkLearningStatus = async () => {
    try {
      const response = await apiCall('/api/keys/status');
      setLearning(response.learningMode);
    } catch (error) {
      console.error('Ошибка проверки статуса обучения:', error);
    }
  };

  useEffect(() => {
    loadKeys();
    checkLearningStatus();
    
    // Проверяем статус каждые 2 секунды
    const interval = setInterval(checkLearningStatus, 2000);
    return () => clearInterval(interval);
  }, []);

  // Начало обучения ключа
  const startLearning = async () => {
    try {
      await apiCall('/api/keys/learn', 'POST');
      addLog('🎓 Режим обучения активирован', 'info');
      // Статус будет обновлен через checkLearningStatus
    } catch (error) {
      addLog('❌ Ошибка активации режима обучения', 'error');
      console.error('Ошибка активации режима обучения:', error);
    }
  };

  // Остановка режима обучения
  const stopLearning = async () => {
    try {
      await apiCall('/api/keys/stop', 'POST');
      addLog('🛑 Режим обучения остановлен', 'warning');
      // Статус будет обновлен через checkLearningStatus
    } catch (error) {
      addLog('❌ Ошибка остановки режима обучения', 'error');
      console.error('Ошибка остановки режима обучения:', error);
    }
  };

  // Удаление ключа
  const deleteKey = async (code: number) => {
    try {
      setLoading(true);
      await apiCall('/api/keys/delete', 'POST', { code });
      setKeys(prev => prev.filter(key => key.code !== code));
      addLog('✅ Ключ удален', 'success');
    } catch (error) {
      addLog('❌ Ошибка удаления ключа', 'error');
      console.error('Ошибка удаления ключа:', error);
    } finally {
      setLoading(false);
      setShowDeleteConfirm(null);
    }
  };

  // Переключение активности ключа
  const toggleKey = async (code: number, enabled: boolean) => {
    try {
      setLoading(true);
      await apiCall('/api/keys/update', 'PUT', { code, enabled });
      setKeys(prev => prev.map(key =>
        key.code === code ? { ...key, enabled } : key
      ));
      addLog(`🔑 Ключ ${enabled ? 'активирован' : 'деактивирован'}`, 'success');
    } catch (error) {
      addLog('❌ Ошибка изменения настроек ключа', 'error');
      console.error('Ошибка изменения настроек ключа:', error);
    } finally {
      setLoading(false);
    }
  };

  // Переименование ключа
  const renameKey = async (code: number, newName: string) => {
    try {
      setLoading(true);
      await apiCall('/api/keys/update', 'PUT', { code, name: newName });
      setKeys(prev => prev.map(key =>
        key.code === code ? { ...key, name: newName } : key
      ));
      addLog('✅ Имя ключа изменено', 'success');
    } catch (error) {
      addLog('❌ Ошибка переименования ключа', 'error');
      console.error('Ошибка переименования ключа:', error);
    } finally {
      setLoading(false);
      setEditingKey(null);
      setEditingName('');
    }
  };

  // Начало редактирования имени
  const startEditing = (key: Key) => {
    setEditingKey(key.code);
    setEditingName(key.name);
  };

  // Сохранение имени
  const saveName = () => {
    if (editingKey && editingName.trim()) {
      renameKey(editingKey, editingName.trim());
    }
  };

  // Отмена редактирования
  const cancelEditing = () => {
    setEditingKey(null);
    setEditingName('');
  };

  return (
    <div className="key-page">
      <div className="key-header">
        <button className="back-btn" onClick={onBack}>
          ← Назад
        </button>
        <h1>🔑 Управление ключами</h1>
      </div>

      <div className="key-controls">
        {!learning ? (
          <button 
            className="learn-btn"
            onClick={startLearning}
            disabled={loading}
          >
            🎓 Обучить новый ключ
          </button>
        ) : (
          <button 
            className="stop-btn"
            onClick={stopLearning}
            disabled={loading}
          >
            🛑 Остановить обучение
          </button>
        )}
      </div>

      {learning && (
        <div className="learning-status">
          <div className="learning-indicator">
            <div className="pulse"></div>
            <span>Держите кнопку на брелке...</span>
          </div>
        </div>
      )}

      <div className="keys-list">
        {keys.length === 0 ? (
          <div className="empty-state">
            <p>📭 Ключи не найдены</p>
            <p>Нажмите "Обучить новый ключ" для добавления</p>
          </div>
        ) : (
          keys.map((key) => (
            <div key={key.code} className={`key-item ${!key.enabled ? 'disabled' : ''}`}>
              <div className="key-info">
                {editingKey === key.code ? (
                  <div className="edit-name">
                    <input
                      type="text"
                      value={editingName}
                      onChange={(e) => setEditingName(e.target.value)}
                      onKeyPress={(e) => e.key === 'Enter' && saveName()}
                      autoFocus
                    />
                    <div className="edit-buttons">
                      <button onClick={saveName} className="save-btn">✓</button>
                      <button onClick={cancelEditing} className="cancel-btn">✗</button>
                    </div>
                  </div>
                ) : (
                  <div className="key-name" onClick={() => startEditing(key)}>
                    <span className="name">{key.name}</span>
                    <span className="edit-hint">✏️</span>
                  </div>
                )}
                <div className="key-details">
                  <span className="code">Код: {key.code}</span>
                  <span className="protocol">Протокол: {key.protocol}</span>
                  <span className="bits">Бит: {key.bitLength}</span>
                </div>
              </div>
              
              <div className="key-actions">
                <button
                  className={`toggle-btn ${key.enabled ? 'enabled' : 'disabled'}`}
                  onClick={() => toggleKey(key.code, !key.enabled)}
                  disabled={loading}
                >
                  {key.enabled ? '✅ Активен' : '❌ Неактивен'}
                </button>
                
                <button
                  className="delete-btn"
                  onClick={() => setShowDeleteConfirm(key.code)}
                  disabled={loading}
                >
                  🗑️ Удалить
                </button>
              </div>
            </div>
          ))
        )}
      </div>

      {showDeleteConfirm && (
        <div className="modal-overlay">
          <div className="modal">
            <h3>Подтверждение удаления</h3>
            <p>Вы уверены, что хотите удалить этот ключ?</p>
            <div className="modal-buttons">
              <button
                className="confirm-btn"
                onClick={() => deleteKey(showDeleteConfirm)}
                disabled={loading}
              >
                Да, удалить
              </button>
              <button
                className="cancel-btn"
                onClick={() => setShowDeleteConfirm(null)}
                disabled={loading}
              >
                Отмена
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default KeyPage;
