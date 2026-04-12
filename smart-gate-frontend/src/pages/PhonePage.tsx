import React, { useState, useEffect, useCallback } from 'react';
import { useApp } from '../App';

interface Phone {
  id: string;
  number: string;
  smsEnabled: boolean;
  callEnabled: boolean;
}

const PhonePage: React.FC = () => {
  const { apiCall, addLog } = useApp();
  const [phones, setPhones] = useState<Phone[]>([]);
  const [loading, setLoading] = useState(true);
  const [newPhone, setNewPhone] = useState('');
  const [deleteTarget, setDeleteTarget] = useState<string | null>(null);

  const loadPhones = useCallback(async () => {
    try {
      const data = await apiCall('/api/phones');
      if (Array.isArray(data)) setPhones(data);
    } catch {
      addLog('Ошибка загрузки телефонов', 'error');
    } finally {
      setLoading(false);
    }
  }, [apiCall, addLog]);

  useEffect(() => { loadPhones(); }, [loadPhones]);

  const addPhone = async () => {
    const digits = newPhone.replace(/\D/g, '');
    if (digits.length !== 10) {
      addLog('Номер должен содержать 10 цифр', 'warning');
      return;
    }
    try {
      const fullNumber = `+7${digits}`;
      const data = await apiCall('/api/phones', 'POST', {
        number: fullNumber,
        smsEnabled: true,
        callEnabled: true,
      });
      setPhones(prev => [...prev, data]);
      setNewPhone('');
      addLog(`Телефон ${fullNumber} добавлен`, 'success');
    } catch {
      addLog('Ошибка добавления телефона', 'error');
    }
  };

  const deletePhone = async (id: string) => {
    try {
      await apiCall('/api/phones/delete', 'POST', { id });
      setPhones(prev => prev.filter(p => p.id !== id));
      addLog('Телефон удалён', 'success');
    } catch {
      addLog('Ошибка удаления', 'error');
    } finally {
      setDeleteTarget(null);
    }
  };

  const toggleSms = async (id: string, enabled: boolean) => {
    try {
      await apiCall('/api/phones/update', 'PUT', { id, smsEnabled: enabled });
      setPhones(prev => prev.map(p => p.id === id ? { ...p, smsEnabled: enabled } : p));
    } catch {
      addLog('Ошибка обновления SMS', 'error');
    }
  };

  const toggleCall = async (id: string, enabled: boolean) => {
    try {
      await apiCall('/api/phones/update', 'PUT', { id, callEnabled: enabled });
      setPhones(prev => prev.map(p => p.id === id ? { ...p, callEnabled: enabled } : p));
    } catch {
      addLog('Ошибка обновления звонков', 'error');
    }
  };

  return (
    <div>
      <div className="page-title">Телефоны</div>

      {/* Add phone */}
      <div className="section" style={{ padding: 14 }}>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <span style={{ color: 'var(--text-dim)', fontWeight: 600, fontSize: 14, flexShrink: 0 }}>+7</span>
          <input
            className="input"
            type="tel"
            value={newPhone}
            onChange={e => {
              const v = e.target.value.replace(/\D/g, '');
              if (v.length <= 10) setNewPhone(v);
            }}
            onKeyDown={e => e.key === 'Enter' && addPhone()}
            placeholder="9001234567"
            maxLength={10}
            style={{ fontFamily: 'var(--mono)' }}
          />
          <button className="btn btn--primary" onClick={addPhone} disabled={newPhone.replace(/\D/g, '').length !== 10}>
            Добавить
          </button>
        </div>
      </div>

      {/* Phone list */}
      {loading ? (
        <div className="empty"><div className="empty-title">Загрузка...</div></div>
      ) : phones.length === 0 ? (
        <div className="section">
          <div className="empty">
            <div className="empty-title">Нет телефонов</div>
            <div className="empty-sub">Добавьте номер для управления воротами по SMS/звонку</div>
          </div>
        </div>
      ) : (
        <div className="section">
          <div className="section-header">Зарегистрированные ({phones.length})</div>
          {phones.map(phone => (
            <div key={phone.id} className="list-item">
              <div className="list-item-body">
                <div className="list-item-title" style={{ fontFamily: 'var(--mono)' }}>{phone.number}</div>
                <div className="list-item-sub" style={{ display: 'flex', gap: 6, marginTop: 4 }}>
                  <span className={`badge badge--${phone.smsEnabled ? 'green' : 'red'}`}>
                    SMS {phone.smsEnabled ? 'вкл' : 'выкл'}
                  </span>
                  <span className={`badge badge--${phone.callEnabled ? 'green' : 'red'}`}>
                    Звонок {phone.callEnabled ? 'вкл' : 'выкл'}
                  </span>
                </div>
              </div>
              <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
                <label className="toggle" title="SMS">
                  <input type="checkbox" checked={phone.smsEnabled} onChange={() => toggleSms(phone.id, !phone.smsEnabled)} />
                  <span className="toggle-track" />
                </label>
                <label className="toggle" title="Звонок">
                  <input type="checkbox" checked={phone.callEnabled} onChange={() => toggleCall(phone.id, !phone.callEnabled)} />
                  <span className="toggle-track" />
                </label>
                <button
                  className="btn btn--ghost"
                  style={{ padding: '6px 10px', fontSize: 12 }}
                  onClick={() => setDeleteTarget(phone.id)}
                >
                  Удалить
                </button>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* Delete modal */}
      {deleteTarget && (
        <div className="modal-overlay" onClick={() => setDeleteTarget(null)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <div className="modal-title">Удалить телефон?</div>
            <div className="modal-text">
              {phones.find(p => p.id === deleteTarget)?.number} будет удалён.
            </div>
            <div className="modal-actions">
              <button className="btn btn--ghost" onClick={() => setDeleteTarget(null)}>Отмена</button>
              <button className="btn btn--danger" onClick={() => deletePhone(deleteTarget)}>Удалить</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default PhonePage;
