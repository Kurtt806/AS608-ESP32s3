/**
 * AS608 Control Panel - JavaScript
 * Realtime WebSocket communication
 */

// ============================================================================
// Configuration
// ============================================================================
const WS_RECONNECT_INTERVAL = 3000;
const TOAST_DURATION = 3000;
const API_BASE = '';

// ============================================================================
// State
// ============================================================================
let ws = null;
let wsReconnectTimer = null;
let currentState = 'IDLE';
let fingerprints = [];

// ============================================================================
// DOM Elements
// ============================================================================
const elements = {
    wifiStatus: document.getElementById('wifi-status'),
    sensorStatus: document.getElementById('sensor-status'),
    stateBadge: document.getElementById('state-badge'),
    fingerCount: document.getElementById('finger-count'),
    librarySize: document.getElementById('library-size'),
    btnEnroll: document.getElementById('btn-enroll'),
    btnSearch: document.getElementById('btn-search'),
    btnCancel: document.getElementById('btn-cancel'),
    btnDeleteAll: document.getElementById('btn-delete-all'),
    fingerList: document.getElementById('finger-list'),
    eventLog: document.getElementById('event-log'),
    volume: document.getElementById('volume'),
    volumeVal: document.getElementById('volume-val'),
    autoSearch: document.getElementById('auto-search'),
    toast: document.getElementById('toast'),
    modal: document.getElementById('modal'),
    modalIcon: document.getElementById('modal-icon'),
    modalTitle: document.getElementById('modal-title'),
    modalMessage: document.getElementById('modal-message'),
    modalConfirm: document.getElementById('modal-confirm')
};

// ============================================================================
// WebSocket
// ============================================================================
function connectWebSocket() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${location.host}/ws`;
    
    ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        logEvent('Connected to device', 'success');
        elements.wifiStatus.classList.add('active');
        elements.wifiStatus.textContent = '📶';
        clearTimeout(wsReconnectTimer);
        
        // Request initial status
        sendCommand('get_status');
        sendCommand('get_fingerprints');
    };
    
    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            handleMessage(data);
        } catch (e) {
            console.error('Failed to parse message:', e);
        }
    };
    
    ws.onclose = () => {
        elements.wifiStatus.classList.remove('active');
        elements.wifiStatus.textContent = '📵';
        logEvent('Disconnected from device', 'warning');
        scheduleReconnect();
    };
    
    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        logEvent('Connection error', 'error');
    };
}

function scheduleReconnect() {
    clearTimeout(wsReconnectTimer);
    wsReconnectTimer = setTimeout(() => {
        logEvent('Reconnecting...', 'info');
        connectWebSocket();
    }, WS_RECONNECT_INTERVAL);
}

function sendCommand(cmd, params = {}) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ cmd, ...params }));
    } else {
        showToast('Not connected to device', 'error');
    }
}

// ============================================================================
// Message Handlers
// ============================================================================
function handleMessage(data) {
    switch (data.type) {
        case 'status':
            updateStatus(data);
            break;
        case 'fingerprints':
            updateFingerprintList(data.list);
            break;
        case 'event':
            handleEvent(data);
            break;
        case 'state':
            updateState(data.state);
            break;
        case 'error':
            showToast(data.message, 'error');
            logEvent(data.message, 'error');
            break;
        case 'success':
            showToast(data.message, 'success');
            logEvent(data.message, 'success');
            break;
    }
}

function updateStatus(data) {
    elements.fingerCount.textContent = data.finger_count ?? '--';
    elements.librarySize.textContent = data.library_size ?? '--';
    
    if (data.sensor_ok) {
        elements.sensorStatus.textContent = '🟢';
        elements.sensorStatus.title = 'Sensor: OK';
        elements.sensorStatus.classList.add('active');
    } else {
        elements.sensorStatus.textContent = '🔴';
        elements.sensorStatus.title = 'Sensor: Error';
        elements.sensorStatus.classList.remove('active');
    }
    
    if (data.state) {
        updateState(data.state);
    }
    
    if (data.volume !== undefined) {
        elements.volume.value = data.volume;
        elements.volumeVal.textContent = data.volume + '%';
    }
    
    if (data.auto_search !== undefined) {
        elements.autoSearch.checked = data.auto_search;
    }
}

function updateState(state) {
    currentState = state;
    const badge = elements.stateBadge;
    
    badge.textContent = state;
    badge.className = 'badge';
    
    switch (state.toUpperCase()) {
        case 'IDLE':
            badge.classList.add('idle');
            setButtonsState(false);
            break;
        case 'ENROLLING':
            badge.classList.add('enrolling');
            setButtonsState(true);
            break;
        case 'SEARCHING':
            badge.classList.add('searching');
            setButtonsState(true);
            break;
        case 'SUCCESS':
            badge.classList.add('success');
            setButtonsState(false);
            break;
        case 'ERROR':
            badge.classList.add('error');
            setButtonsState(false);
            break;
    }
}

function setButtonsState(inProgress) {
    elements.btnEnroll.disabled = inProgress;
    elements.btnSearch.disabled = inProgress;
    elements.btnDeleteAll.disabled = inProgress;
    elements.btnCancel.disabled = !inProgress;
}

function updateFingerprintList(list) {
    fingerprints = list || [];
    
    if (fingerprints.length === 0) {
        elements.fingerList.innerHTML = '<li class="empty-state">No fingerprints enrolled</li>';
        return;
    }
    
    elements.fingerList.innerHTML = fingerprints.map((fp, idx) => `
        <li class="finger-item" style="animation-delay: ${idx * 0.05}s">
            <div class="finger-info">
                <span class="finger-id">${fp.id}</span>
                <span class="finger-name">${fp.name || 'Fingerprint ' + fp.id}</span>
            </div>
            <button class="delete-btn" onclick="deleteFinger(${fp.id})" title="Delete">🗑️</button>
        </li>
    `).join('');
}

function handleEvent(data) {
    const { event, message } = data;
    
    switch (event) {
        case 'enroll_start':
            logEvent('Enrollment started - place finger', 'info');
            showToast('Place your finger on the sensor', 'info');
            break;
        case 'enroll_step':
            logEvent(`Step ${data.step}/3 - place finger again`, 'info');
            showToast(`Step ${data.step}/3 - place finger again`, 'info');
            break;
        case 'enroll_ok':
            logEvent(`Enrolled fingerprint #${data.id}`, 'success');
            showToast(`Fingerprint #${data.id} enrolled!`, 'success');
            sendCommand('get_fingerprints');
            sendCommand('get_status');
            break;
        case 'match_ok':
            logEvent(`Matched fingerprint #${data.id} (${data.score})`, 'success');
            showToast(`✓ Matched #${data.id}`, 'success');
            break;
        case 'match_fail':
            logEvent('No match found', 'warning');
            showToast('No match found', 'warning');
            break;
        case 'delete_ok':
            logEvent(`Deleted fingerprint #${data.id}`, 'success');
            sendCommand('get_fingerprints');
            sendCommand('get_status');
            break;
        case 'delete_all':
            logEvent('All fingerprints deleted', 'success');
            sendCommand('get_fingerprints');
            sendCommand('get_status');
            break;
        case 'error':
            logEvent(message || 'An error occurred', 'error');
            showToast(message || 'Error', 'error');
            break;
        default:
            if (message) {
                logEvent(message, 'info');
            }
    }
}

// ============================================================================
// Actions
// ============================================================================
function startEnroll() {
    sendCommand('enroll');
    updateState('ENROLLING');
    logEvent('Starting enrollment...', 'info');
}

function startSearch() {
    sendCommand('search');
    updateState('SEARCHING');
    logEvent('Starting search...', 'info');
}

function cancelAction() {
    sendCommand('cancel');
    updateState('IDLE');
    logEvent('Action cancelled', 'warning');
}

function deleteFinger(id) {
    showModal(
        '🗑️',
        'Delete Fingerprint',
        `Delete fingerprint #${id}?`,
        () => {
            sendCommand('delete', { id });
            closeModal();
        }
    );
}

function deleteAll() {
    showModal(
        '⚠️',
        'Delete All',
        'This will delete all enrolled fingerprints. Are you sure?',
        () => {
            sendCommand('delete_all');
            closeModal();
        }
    );
}

function refreshList() {
    sendCommand('get_fingerprints');
    sendCommand('get_status');
    logEvent('Refreshing...', 'info');
}

function setVolume(value) {
    elements.volumeVal.textContent = value + '%';
    sendCommand('set_volume', { volume: parseInt(value) });
}

function setAutoSearch(enabled) {
    sendCommand('set_auto_search', { enabled });
    logEvent(`Auto search ${enabled ? 'enabled' : 'disabled'}`, 'info');
}

// ============================================================================
// UI Helpers
// ============================================================================
function logEvent(message, type = 'info') {
    const timestamp = new Date().toLocaleTimeString();
    const item = document.createElement('div');
    item.className = `event-item ${type}`;
    item.innerHTML = `<span style="color:var(--text-secondary)">[${timestamp}]</span> ${message}`;
    
    elements.eventLog.insertBefore(item, elements.eventLog.firstChild);
    
    // Keep only last 50 events
    while (elements.eventLog.children.length > 50) {
        elements.eventLog.removeChild(elements.eventLog.lastChild);
    }
}

function clearLog() {
    elements.eventLog.innerHTML = '<div class="event-item info">Log cleared</div>';
}

let toastTimer = null;
function showToast(message, type = 'info') {
    clearTimeout(toastTimer);
    
    const icons = {
        success: '✓',
        error: '✗',
        warning: '⚠',
        info: 'ℹ'
    };
    
    elements.toast.innerHTML = `<span>${icons[type] || ''}</span> ${message}`;
    elements.toast.className = `toast ${type} show`;
    
    toastTimer = setTimeout(() => {
        elements.toast.classList.remove('show');
    }, TOAST_DURATION);
}

function showModal(icon, title, message, onConfirm) {
    elements.modalIcon.textContent = icon;
    elements.modalTitle.textContent = title;
    elements.modalMessage.textContent = message;
    elements.modalConfirm.onclick = onConfirm;
    elements.modal.classList.add('show');
}

function closeModal() {
    elements.modal.classList.remove('show');
}

// Close modal on background click
elements.modal.addEventListener('click', (e) => {
    if (e.target === elements.modal) {
        closeModal();
    }
});

// Escape key to close modal
document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
        closeModal();
    }
});

// ============================================================================
// Initialization
// ============================================================================
document.addEventListener('DOMContentLoaded', () => {
    logEvent('Initializing...', 'info');
    connectWebSocket();
});

// Handle page visibility
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            connectWebSocket();
        }
    }
});
