import React, { useState, useEffect } from 'react';
import './PhonePage.css';

interface Phone {
  id: string; // Теперь ID - это номер телефона
  number: string;
  smsEnabled: boolean;
  callEnabled: boolean;
}

interface PhonePageProps {
  onBack: () => void;
  apiCall: (endpoint: string, method?: string, data?: any) => Promise<any>;
  addLog: (message: string, type?: 'info' | 'error' | 'success' | 'warning') => void;
}

const PhonePage: React.FC<PhonePageProps> = ({ onBack, apiCall, addLog }) => {
  const [phones, setPhones] = useState<Phone[]>([]);
  const [newPhone, setNewPhone] = useState('');
  const [loading, setLoading] = useState(false);
  const [showDeleteConfirm, setShowDeleteConfirm] = useState<string | null>(null);

  // Загрузка списка телефонов
  const loadPhones = async () => {
    try {
      setLoading(true);
      const data = await apiCall('/api/phones');
      setPhones(data);
      addLog(`📱 Загружено ${data.length} телефонов`, 'success');
    } catch (error) {
      addLog('❌ Ошибка загрузки телефонов', 'error');
      console.error('Ошибка загрузки телефонов:', error);
    } finally {
      setLoading(false);
    }
  };

  // Добавление нового телефона
  const addPhone = async () => {
    if (!newPhone.trim()) {
      addLog('⚠️ Введите номер телефона', 'warning');
      return;
    }

    // Проверка формата номера (только цифры после +7)
    const phoneNumber = newPhone.replace(/\D/g, '');
    if (phoneNumber.length !== 10) {
      addLog('⚠️ Номер должен содержать 10 цифр', 'warning');
      return;
    }

    try {
      setLoading(true);
      const fullNumber = `+7${phoneNumber}`;
      const data = await apiCall('/api/phones', 'POST', { 
        number: fullNumber,
        smsEnabled: true,
        callEnabled: true
      });
      
      setPhones(prev => [...prev, data]);
      setNewPhone('');
      addLog(`✅ Телефон ${fullNumber} добавлен`, 'success');
    } catch (error) {
      addLog('❌ Ошибка добавления телефона', 'error');
      console.error('Ошибка добавления телефона:', error);
    } finally {
      setLoading(false);
    }
  };

  // Удаление телефона
  const deletePhone = async (id: string) => {
    try {
      setLoading(true);
      await apiCall('/api/phones/delete', 'POST', { id });
      setPhones(prev => prev.filter(phone => phone.id !== id));
      addLog('✅ Телефон удален', 'success');
    } catch (error) {
      addLog('❌ Ошибка удаления телефона', 'error');
      console.error('Ошибка удаления телефона:', error);
    } finally {
      setLoading(false);
      setShowDeleteConfirm(null);
    }
  };

  // Переключение SMS
  const toggleSMS = async (id: string, enabled: boolean) => {
    try {
      setLoading(true);
      await apiCall('/api/phones/update', 'PUT', { id, smsEnabled: enabled });
      setPhones(prev => prev.map(phone => 
        phone.id === id ? { ...phone, smsEnabled: enabled } : phone
      ));
      addLog(`📱 SMS ${enabled ? 'включен' : 'отключен'} для телефона`, 'success');
    } catch (error) {
      addLog('❌ Ошибка изменения настроек SMS', 'error');
      console.error('Ошибка изменения настроек SMS:', error);
    } finally {
      setLoading(false);
    }
  };

  // Переключение звонков
  const toggleCall = async (id: string, enabled: boolean) => {
    try {
      setLoading(true);
      await apiCall('/api/phones/update', 'PUT', { id, callEnabled: enabled });
      setPhones(prev => prev.map(phone => 
        phone.id === id ? { ...phone, callEnabled: enabled } : phone
      ));
      addLog(`📞 Звонки ${enabled ? 'включены' : 'отключены'} для телефона`, 'success');
    } catch (error) {
      addLog('❌ Ошибка изменения настроек звонков', 'error');
      console.error('Ошибка изменения настроек звонков:', error);
    } finally {
      setLoading(false);
    }
  };

  // Обработка ввода номера телефона
  const handlePhoneInput = (e: React.ChangeEvent<HTMLInputElement>) => {
    const value = e.target.value.replace(/\D/g, ''); // Только цифры
    if (value.length <= 10) {
      setNewPhone(value);
    }
  };

  // Обработка нажатия Enter
  const handleKeyPress = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      addPhone();
    }
  };

  useEffect(() => {
    loadPhones();
  }, []);

  return (
    <div className="phone-page">
      <div className="page-header">
        <button className="btn-back" onClick={onBack}>← Назад</button>
        <h2>📱 Управление телефонами</h2>
      </div>

      {/* Форма добавления телефона */}
      <div className="add-phone-section">
        <h3>Добавить новый телефон</h3>
        <div className="phone-input-group">
          <div className="phone-prefix">+7</div>
          <input
            type="text"
            className="phone-input"
            placeholder="Введите номер телефона"
            value={newPhone}
            onChange={handlePhoneInput}
            onKeyPress={handleKeyPress}
            maxLength={10}
            disabled={loading}
          />
          <button 
            className="btn btn-success"
            onClick={addPhone}
            disabled={loading || !newPhone.trim()}
          >
            {loading ? '⏳' : '➕'}
          </button>
        </div>
        <div className="phone-hint">
          Введите 10 цифр номера телефона (без +7)
        </div>
      </div>

      {/* Список телефонов */}
      <div className="phones-section">
        <h3>Зарегистрированные телефоны ({phones.length})</h3>
        
        {loading && phones.length === 0 ? (
          <div className="loading">⏳ Загрузка...</div>
        ) : phones.length === 0 ? (
          <div className="empty-state">
            <div className="empty-icon">📱</div>
            <div className="empty-text">Телефоны не добавлены</div>
            <div className="empty-hint">Добавьте первый телефон для управления воротами</div>
          </div>
        ) : (
          <div className="phones-list">
            {phones.map((phone) => (
              <div key={phone.id} className="phone-item">
                <div className="phone-info">
                  <div className="phone-number">{phone.number}</div>
                  <div className="phone-status">
                    <span className={`status-badge ${phone.smsEnabled ? 'active' : 'inactive'}`}>
                      📱 SMS {phone.smsEnabled ? 'ВКЛ' : 'ВЫКЛ'}
                    </span>
                    <span className={`status-badge ${phone.callEnabled ? 'active' : 'inactive'}`}>
                      📞 Звонок {phone.callEnabled ? 'ВКЛ' : 'ВЫКЛ'}
                    </span>
                  </div>
                </div>
                
                <div className="phone-controls">
                  <button
                    className={`btn-toggle ${phone.smsEnabled ? 'active' : 'inactive'}`}
                    onClick={() => toggleSMS(phone.id, !phone.smsEnabled)}
                    disabled={loading}
                    title={`${phone.smsEnabled ? 'Отключить' : 'Включить'} SMS`}
                  >
                    📱
                  </button>
                  
                  <button
                    className={`btn-toggle ${phone.callEnabled ? 'active' : 'inactive'}`}
                    onClick={() => toggleCall(phone.id, !phone.callEnabled)}
                    disabled={loading}
                    title={`${phone.callEnabled ? 'Отключить' : 'Включить'} звонки`}
                  >
                    📞
                  </button>
                  
                  <button
                    className="btn-delete"
                    onClick={() => setShowDeleteConfirm(phone.id)}
                    disabled={loading}
                    title="Удалить телефон"
                  >
                    🗑️
                  </button>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Модальное окно подтверждения удаления */}
      {showDeleteConfirm && (
        <div className="modal-overlay" onClick={() => setShowDeleteConfirm(null)}>
          <div className="modal-content" onClick={(e) => e.stopPropagation()}>
            <h3>Подтверждение удаления</h3>
            <p>Вы уверены, что хотите удалить этот телефон?</p>
            <p className="phone-to-delete">
              {phones.find(p => p.id === showDeleteConfirm)?.number}
            </p>
            <div className="modal-buttons">
              <button 
                className="btn btn-danger"
                onClick={() => deletePhone(showDeleteConfirm)}
                disabled={loading}
              >
                {loading ? '⏳' : '🗑️ Удалить'}
              </button>
              <button 
                className="btn"
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

export default PhonePage;
