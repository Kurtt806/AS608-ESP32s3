/**
 * app.js - Main Application Controller
 * AS608 Dashboard - Real-time Event-Based UI
 */

(function() {
    'use strict';
    
    // ========================================================================
    // DOM Elements
    // ========================================================================
    const DOM = {
        // Connection status
        wsStatus: document.getElementById('ws-status'),
        wsText: document.getElementById('ws-text'),
        
        // Event banner
        eventBanner: document.getElementById('event-banner'),
        eventText: document.getElementById('event-text'),
        
        // Status display
        sensorStatus: document.getElementById('sensor-status'),
        fingerCount: document.getElementById('finger-count'),
        librarySize: document.getElementById('library-size'),
        nextId: document.getElementById('next-id'),
        stateBadge: document.getElementById('state-badge'),
        
        // Buttons
        btnEnroll: document.getElementById('btn-enroll'),
        btnMatch: document.getElementById('btn-match'),
        btnDelete: document.getElementById('btn-delete'),
        btnClear: document.getElementById('btn-clear'),
        btnCancel: document.getElementById('btn-cancel'),
        btnRefresh: document.getElementById('btn-refresh'),
        
        // Volume
        volumeSlider: document.getElementById('volume-slider'),
        volumeValue: document.getElementById('volume-value'),
        
        // Fingerprint list
        fingerList: document.getElementById('finger-list'),
        
        // Delete modal
        deleteModal: document.getElementById('delete-modal'),
        deleteIdInput: document.getElementById('delete-id-input'),
        deleteModalCancel: document.getElementById('delete-modal-cancel'),
        deleteModalConfirm: document.getElementById('delete-modal-confirm'),
        
        // Rename modal
        renameModal: document.getElementById('rename-modal'),
        renameIdDisplay: document.getElementById('rename-id-display'),
        renameInput: document.getElementById('rename-input'),
        renameModalCancel: document.getElementById('rename-modal-cancel'),
        renameModalConfirm: document.getElementById('rename-modal-confirm'),
        
        // Confirm modal
        confirmModal: document.getElementById('confirm-modal'),
        confirmTitle: document.getElementById('confirm-title'),
        confirmMessage: document.getElementById('confirm-message'),
        confirmCancel: document.getElementById('confirm-cancel'),
        confirmOk: document.getElementById('confirm-ok'),
        
        // OTA
        otaVersion: document.getElementById('ota-version'),
        otaState: document.getElementById('ota-state'),
        otaFile: document.getElementById('ota-file'),
        otaFilename: document.getElementById('ota-filename'),
        btnOtaSelect: document.getElementById('btn-ota-select'),
        btnOtaUpload: document.getElementById('btn-ota-upload'),
        otaProgressContainer: document.getElementById('ota-progress-container'),
        otaProgressBar: document.getElementById('ota-progress-bar'),
        otaProgressText: document.getElementById('ota-progress-text'),
        
        // Toast
        toast: document.getElementById('toast'),
        toastMessage: document.getElementById('toast-message')
    };
    
    // ========================================================================
    // State
    // ========================================================================
    let currentState = 'IDLE';
    let toastTimer = null;
    let confirmCallback = null;
    let renameTargetId = -1;
    let isNewFingerprint = false;  // Track if naming a new fingerprint
    
    // ========================================================================
    // Event Banner Messages
    // ========================================================================
    const EVENT_MESSAGES = {
        'idle': { text: 'Sẵn sàng', style: 'idle' },
        'finger_detected': { text: 'Đã nhận vân tay', style: '' },
        'enroll_step1_ok': { text: 'Bước 1/2 - OK', style: '' },
        'enroll_step2_ok': { text: 'Bước 2/2 - OK', style: 'success' },
        'remove_finger': { text: 'Nhấc tay lên', style: 'warning' },
        'saving': { text: 'Đang lưu...', style: '' },
        'store_ok': { text: 'Đã lưu!', style: 'success' },
        'store_fail': { text: 'Lưu thất bại', style: 'error' },
        'match_ok': { text: 'Xác nhận!', style: 'success' },
        'no_match': { text: 'Không khớp', style: 'error' },
        'error': { text: 'Lỗi', style: 'error' },
        'enrolling': { text: 'Đặt ngón tay...', style: '' },
        'searching': { text: 'Đang quét...', style: '' }
    };
    
    // ========================================================================
    // UI Update Functions
    // ========================================================================
    
    /**
     * Update event banner with real-time event
     */
    function updateEventBanner(event, data = {}) {
        const info = EVENT_MESSAGES[event] || { text: event, style: '' };
        let text = info.text;
        
        // Handle match with ID/name
        if (event === 'match_ok') {
            if (data.name) {
                text = `✓ ${data.name}`;
            } else if (data.id !== undefined) {
                text = `✓ ID: ${data.id}`;
            }
        }
        
        DOM.eventText.textContent = text;
        DOM.eventBanner.className = 'event-banner ' + info.style;
    }
    
    /**
     * Update connection status display
     */
    function updateConnectionStatus(connected) {
        if (connected) {
            DOM.wsStatus.classList.add('connected');
            DOM.wsText.textContent = 'Connected';
        } else {
            DOM.wsStatus.classList.remove('connected');
            DOM.wsText.textContent = 'Disconnected';
        }
    }
    
    /**
     * Update device status display
     */
    function updateStatus(data) {
        if (data.sensor_ok !== undefined) {
            DOM.sensorStatus.textContent = data.sensor_ok ? 'OK' : 'Error';
            DOM.sensorStatus.className = 'status-value ' + (data.sensor_ok ? 'ok' : 'error');
        }
        
        if (data.finger_count !== undefined) {
            DOM.fingerCount.textContent = data.finger_count;
        }
        
        if (data.library_size !== undefined) {
            DOM.librarySize.textContent = data.library_size;
        }
        
        if (data.next_id !== undefined) {
            DOM.nextId.textContent = data.next_id;
        }
        
        if (data.volume !== undefined) {
            DOM.volumeSlider.value = data.volume;
            DOM.volumeValue.textContent = data.volume + '%';
            updateSliderBackground(DOM.volumeSlider);
        }
        
        if (data.state) {
            updateState(data.state);
        }
    }
    
    /**
     * Update state badge
     */
    function updateState(state) {
        currentState = state.toUpperCase();
        DOM.stateBadge.textContent = currentState;
        DOM.stateBadge.className = 'state-badge ' + state.toLowerCase();
        
        // Update button states
        const inProgress = currentState === 'ENROLLING' || currentState === 'SEARCHING' || currentState === 'MATCHING';
        DOM.btnEnroll.disabled = inProgress;
        DOM.btnMatch.disabled = inProgress;
        DOM.btnDelete.disabled = inProgress;
        DOM.btnClear.disabled = inProgress;
        DOM.btnCancel.disabled = !inProgress;
        
        // Update event banner based on state
        if (currentState === 'IDLE') {
            updateEventBanner('idle');
        } else if (currentState === 'ENROLLING') {
            updateEventBanner('enrolling');
        } else if (currentState === 'SEARCHING' || currentState === 'MATCHING') {
            updateEventBanner('searching');
        }
    }
    
    /**
     * Update fingerprint list
     */
    function updateFingerprintList(data) {
        const list = data.list || [];
        
        if (list.length === 0) {
            DOM.fingerList.innerHTML = '<li class="empty-state">Chưa có vân tay nào</li>';
            return;
        }
        
        DOM.fingerList.innerHTML = list.map(fp => `
            <li class="finger-item">
                <div class="finger-info">
                    <span class="finger-id">ID: ${fp.id}</span>
                    <span class="finger-name" title="Nhấn để đổi tên" onclick="App.renameFinger(${fp.id}, '${escapeHtml(fp.name || '')}')">${escapeHtml(fp.name || 'ID_' + fp.id)}</span>
                    ${fp.match_count ? `<span class="finger-matches">(${fp.match_count} lần)</span>` : ''}
                </div>
                <div class="finger-actions">
                    <button class="btn-icon btn-edit" onclick="App.renameFinger(${fp.id}, '${escapeHtml(fp.name || '')}')" title="Đổi tên">✏️</button>
                    <button class="btn-icon btn-delete-small" onclick="App.deleteFingerById(${fp.id})" title="Xóa">🗑️</button>
                </div>
            </li>
        `).join('');
    }
    
    /**
     * Escape HTML special characters
     */
    function escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML.replace(/'/g, "\\'");
    }
    
    /**
     * Update slider background
     */
    function updateSliderBackground(slider) {
        const value = (slider.value - slider.min) / (slider.max - slider.min) * 100;
        slider.style.background = `linear-gradient(to right, #FF7A00 0%, #FF7A00 ${value}%, #D2D2D7 ${value}%, #D2D2D7 100%)`;
    }
    
    /**
     * Show toast notification
     */
    function showToast(message, type = 'info') {
        clearTimeout(toastTimer);
        
        DOM.toastMessage.textContent = message;
        DOM.toast.className = 'toast show ' + type;
        
        toastTimer = setTimeout(() => {
            DOM.toast.classList.remove('show');
        }, 3000);
    }
    
    /**
     * Show delete modal
     */
    function showDeleteModal() {
        DOM.deleteIdInput.value = '';
        DOM.deleteModal.classList.add('show');
        DOM.deleteIdInput.focus();
    }
    
    /**
     * Hide delete modal
     */
    function hideDeleteModal() {
        DOM.deleteModal.classList.remove('show');
    }
    
    /**
     * Show confirm modal
     */
    function showConfirmModal(title, message, callback) {
        DOM.confirmTitle.textContent = title;
        DOM.confirmMessage.textContent = message;
        confirmCallback = callback;
        DOM.confirmModal.classList.add('show');
    }
    
    /**
     * Hide confirm modal
     */
    function hideConfirmModal() {
        DOM.confirmModal.classList.remove('show');
        confirmCallback = null;
    }
    
    /**
     * Show rename modal
     */
    function showRenameModal(id, currentName) {
        renameTargetId = id;
        isNewFingerprint = false;
        
        // Reset modal to default state
        const modalTitle = DOM.renameModal.querySelector('h3');
        if (modalTitle) {
            modalTitle.textContent = 'Đổi tên vân tay';
        }
        
        DOM.renameIdDisplay.textContent = id;
        DOM.renameInput.value = currentName || '';
        DOM.renameInput.placeholder = 'Nhập tên mới';
        DOM.renameModalCancel.textContent = 'Hủy';
        
        DOM.renameModal.classList.add('show');
        DOM.renameInput.focus();
        DOM.renameInput.select();
    }
    
    /**
     * Hide rename modal
     */
    function hideRenameModal() {
        DOM.renameModal.classList.remove('show');
        
        // If skipped naming for new fingerprint, show confirmation
        if (isNewFingerprint && renameTargetId >= 0) {
            showToast(`Vân tay ID ${renameTargetId} đã được lưu`, 'success');
        }
        
        renameTargetId = -1;
        isNewFingerprint = false;
    }
    
    /**
     * Rename fingerprint
     */
    async function renameFinger(id, currentName) {
        showRenameModal(id, currentName);
    }
    
    /**
     * Confirm rename
     */
    async function confirmRename() {
        if (renameTargetId < 0) return;
        
        const newName = DOM.renameInput.value.trim();
        if (!newName) {
            showToast('Tên không được để trống', 'error');
            return;
        }
        
        const savedId = renameTargetId;
        const wasNew = isNewFingerprint;
        
        try {
            await API.setName(renameTargetId, newName);
            renameTargetId = -1;  // Clear before hiding to avoid toast
            isNewFingerprint = false;
            DOM.renameModal.classList.remove('show');
            
            if (wasNew) {
                showToast(`Đã lưu: ${newName}`, 'success');
            } else {
                showToast(`Đã đổi tên ID ${savedId}`, 'success');
            }
            refreshData();
        } catch (e) {
            showToast('Lỗi khi lưu tên', 'error');
        }
    }
    
    /**
     * Show naming dialog for new fingerprint
     */
    function showNewFingerprintNaming(id) {
        renameTargetId = id;
        isNewFingerprint = true;
        
        // Update modal for new fingerprint
        const modalTitle = DOM.renameModal.querySelector('h3');
        if (modalTitle) {
            modalTitle.textContent = '🎉 Đăng ký thành công!';
        }
        
        DOM.renameIdDisplay.textContent = id;
        DOM.renameInput.value = '';
        DOM.renameInput.placeholder = 'Nhập tên (VD: Nguyen Van A)';
        DOM.renameModalCancel.textContent = 'Bỏ qua';
        
        DOM.renameModal.classList.add('show');
        DOM.renameInput.focus();
    }
    
    /**
     * Show match result with name
     */
    async function showMatchResult(id, score, name) {
        // Try to get name from API if not provided
        let displayName = name;
        if (!displayName && id >= 0) {
            try {
                const meta = await API.getMeta(id);
                displayName = meta.name;
            } catch (e) {
                displayName = `ID_${id}`;
            }
        }
        
        // Show result modal
        showResultModal(
            '✅ Xác nhận thành công',
            `<div class="match-result">
                <div class="match-name">${escapeHtmlDisplay(displayName || 'ID_' + id)}</div>
                <div class="match-details">
                    <span class="match-id">ID: ${id}</span>
                    <span class="match-score">Độ khớp: ${score || '--'}</span>
                </div>
            </div>`
        );
    }
    
    /**
     * Show result modal (for match results)
     */
    function showResultModal(title, htmlContent) {
        DOM.confirmTitle.textContent = title;
        DOM.confirmMessage.innerHTML = htmlContent;
        DOM.confirmCancel.style.display = 'none';
        DOM.confirmOk.textContent = 'OK';
        confirmCallback = () => {
            hideConfirmModal();
            DOM.confirmCancel.style.display = '';
            DOM.confirmOk.textContent = 'Xác nhận';
        };
        DOM.confirmModal.classList.add('show');
    }
    
    /**
     * Escape HTML for display (different from escapeHtml for attributes)
     */
    function escapeHtmlDisplay(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
    
    // ========================================================================
    // WebSocket Event Handlers
    // ========================================================================
    
    function setupWebSocketEvents() {
        // Connection events
        WS.on('connected', () => {
            updateConnectionStatus(true);
            showToast('Connected', 'success');
            refreshData();
        });
        
        WS.on('disconnected', () => {
            updateConnectionStatus(false);
        });
        
        WS.on('reconnecting', () => {
            showToast('Reconnecting...', 'info');
        });
        
        // Status updates
        WS.on('status', (data) => {
            updateStatus(data);
        });
        
        // Fingerprint list
        WS.on('fingerprints', (data) => {
            updateFingerprintList(data);
        });
        
        // Real-time events
        WS.on('finger_detected', () => {
            updateEventBanner('finger_detected');
        });
        
        WS.on('enroll_step1_ok', () => {
            updateEventBanner('enroll_step1_ok');
        });
        
        WS.on('enroll_step2_ok', () => {
            updateEventBanner('enroll_step2_ok');
        });
        
        WS.on('remove_finger', () => {
            updateEventBanner('remove_finger');
        });
        
        WS.on('saving', () => {
            updateEventBanner('saving');
        });
        
        WS.on('store_ok', (data) => {
            updateEventBanner('store_ok');
            refreshData();
            
            // Show naming dialog for new fingerprint
            if (data.id !== undefined) {
                showNewFingerprintNaming(data.id);
            }
        });
        
        WS.on('store_fail', () => {
            updateEventBanner('store_fail');
            showToast('Failed to save fingerprint', 'error');
        });
        
        WS.on('match', (data) => {
            updateEventBanner('match_ok', data);
            
            // Show match result with name if available
            showMatchResult(data.id, data.score, data.name);
        });
        
        WS.on('no_match', () => {
            updateEventBanner('no_match');
            showToast('No match found', 'error');
        });
        
        WS.on('idle', () => {
            updateState('idle');
        });
        
        WS.on('enrolling', () => {
            updateState('enrolling');
        });
        
        WS.on('searching', () => {
            updateState('searching');
        });
        
        WS.on('error', (data) => {
            updateEventBanner('error');
            showToast(data.message || 'Error occurred', 'error');
        });
        
        WS.on('delete_ok', (data) => {
            showToast(`Fingerprint #${data.id} deleted`, 'success');
            refreshData();
        });
        
        WS.on('clear_ok', () => {
            showToast('All fingerprints cleared', 'success');
            refreshData();
        });
        
        // OTA progress events
        WS.on('ota_progress', (data) => {
            handleOtaProgress(data);
        });
    }
    
    // ========================================================================
    // Actions
    // ========================================================================
    
    /**
     * Refresh status and fingerprint list
     */
    async function refreshData() {
        try {
            const status = await API.getStatus();
            updateStatus(status);
            
            const fps = await API.getFingerprints();
            updateFingerprintList(fps);
            
            // Also refresh OTA status
            refreshOtaStatus();
        } catch (e) {
            console.error('Failed to refresh data:', e);
        }
    }
    
    /**
     * Start enrollment
     */
    async function startEnroll() {
        try {
            await API.enroll();
            updateState('enrolling');
            updateEventBanner('enrolling');
        } catch (e) {
            showToast('Failed to start enrollment', 'error');
        }
    }
    
    /**
     * Start match test
     */
    async function startMatch() {
        try {
            await API.match();
            updateState('searching');
            updateEventBanner('searching');
        } catch (e) {
            showToast('Lỗi khi bắt đầu kiểm tra', 'error');
        }
    }
    
    /**
     * Cancel current operation
     */
    async function cancelOperation() {
        try {
            await API.cancel();
            updateState('idle');
        } catch (e) {
            showToast('Lỗi khi hủy', 'error');
        }
    }
    
    /**
     * Delete fingerprint by ID
     */
    async function deleteFingerById(id) {
        showConfirmModal(
            'Xóa vân tay',
            `Xóa vân tay ID ${id}?`,
            async () => {
                try {
                    await API.deleteId(id);
                    showToast(`Đã xóa ID ${id}`, 'success');
                    refreshData();
                } catch (e) {
                    showToast('Xóa thất bại', 'error');
                }
                hideConfirmModal();
            }
        );
    }
    
    /**
     * Clear all fingerprints
     */
    function clearAllFingerprints() {
        showConfirmModal(
            '⚠️ Xóa tất cả',
            'Xóa TẤT CẢ vân tay? Hành động này không thể hoàn tác!',
            async () => {
                try {
                    await API.clearAll();
                    showToast('Đã xóa tất cả vân tay', 'success');
                    refreshData();
                } catch (e) {
                    showToast('Xóa thất bại', 'error');
                }
                hideConfirmModal();
            }
        );
    }
    
    /**
     * Set volume
     */
    async function setVolume(value) {
        DOM.volumeValue.textContent = value + '%';
        updateSliderBackground(DOM.volumeSlider);
        
        try {
            await API.setVolume(parseInt(value));
        } catch (e) {
            console.error('Failed to set volume:', e);
        }
    }
    
    // ========================================================================
    // OTA Functions
    // ========================================================================
    
    let selectedOtaFile = null;
    let otaInProgress = false;
    
    /**
     * Fetch and display OTA status
     */
    async function refreshOtaStatus() {
        try {
            const status = await API.getOtaStatus();
            DOM.otaVersion.textContent = status.version || '--';
            
            const stateMap = {
                'idle': 'Sẵn sàng',
                'starting': 'Đang bắt đầu',
                'downloading': 'Đang tải',
                'verifying': 'Đang xác thực',
                'applying': 'Đang áp dụng',
                'completed': 'Hoàn tất',
                'failed': 'Thất bại',
                'rollback': 'Đang phục hồi'
            };
            DOM.otaState.textContent = stateMap[status.state] || status.state || '--';
        } catch (e) {
            console.error('Failed to get OTA status:', e);
        }
    }
    
    /**
     * Handle OTA progress WebSocket event
     */
    function handleOtaProgress(data) {
        const stateMap = {
            'idle': 'Sẵn sàng',
            'starting': 'Đang bắt đầu...',
            'downloading': 'Đang tải...',
            'verifying': 'Đang xác thực...',
            'applying': 'Đang áp dụng...',
            'completed': 'Hoàn tất!',
            'failed': 'Thất bại',
            'rollback': 'Đang phục hồi...'
        };
        
        DOM.otaState.textContent = stateMap[data.state] || data.state;
        
        if (data.progress !== undefined) {
            DOM.otaProgressContainer.style.display = 'block';
            DOM.otaProgressBar.style.width = data.progress + '%';
            DOM.otaProgressText.textContent = data.progress + '%';
        }
        
        if (data.state === 'completed') {
            showToast('Cập nhật thành công! Đang khởi động lại...', 'success');
            otaInProgress = false;
            DOM.btnOtaUpload.disabled = false;
            DOM.btnOtaSelect.disabled = false;
        } else if (data.state === 'failed') {
            showToast('Cập nhật thất bại: ' + (data.message || 'Lỗi không xác định'), 'error');
            otaInProgress = false;
            DOM.btnOtaUpload.disabled = false;
            DOM.btnOtaSelect.disabled = false;
            DOM.otaProgressContainer.style.display = 'none';
        } else if (data.state === 'rebooting') {
            showToast('Đang khởi động lại thiết bị...', 'info');
        }
    }
    
    /**
     * Start OTA firmware upload
     */
    async function startOtaUpload() {
        const file = DOM.otaFile.files[0];
        if (!file) {
            showToast('Vui lòng chọn file firmware', 'error');
            return;
        }
        
        if (!file.name.endsWith('.bin')) {
            showToast('File phải có định dạng .bin', 'error');
            return;
        }
        
        // Confirm before upload
        showConfirmModal(
            'Xác nhận cập nhật',
            `Cập nhật firmware từ file "${file.name}" (${(file.size / 1024).toFixed(1)} KB)?`,
            async () => {
                hideConfirmModal();
                await performOtaUpload(file);
            }
        );
    }
    
    /**
     * Perform the actual OTA upload
     */
    async function performOtaUpload(file) {
        otaInProgress = true;
        DOM.btnOtaUpload.disabled = true;
        DOM.btnOtaSelect.disabled = true;
        DOM.otaProgressContainer.style.display = 'block';
        DOM.otaProgressBar.style.width = '0%';
        DOM.otaProgressText.textContent = '0%';
        DOM.otaState.textContent = 'Đang tải lên...';
        
        try {
            const arrayBuffer = await file.arrayBuffer();
            
            await API.uploadFirmware(arrayBuffer, (progress) => {
                DOM.otaProgressBar.style.width = progress + '%';
                DOM.otaProgressText.textContent = progress + '%';
            });
            
            // Upload complete, wait for device to process
            DOM.otaState.textContent = 'Đang xử lý...';
            
        } catch (e) {
            console.error('OTA upload failed:', e);
            showToast('Tải lên thất bại: ' + e.message, 'error');
            otaInProgress = false;
            DOM.btnOtaUpload.disabled = false;
            DOM.btnOtaSelect.disabled = false;
            DOM.otaProgressContainer.style.display = 'none';
            DOM.otaState.textContent = 'Thất bại';
        }
    }
    
    // ========================================================================
    // Event Listeners
    // ========================================================================
    
    function setupEventListeners() {
        // Action buttons
        DOM.btnEnroll.addEventListener('click', startEnroll);
        DOM.btnMatch.addEventListener('click', startMatch);
        DOM.btnCancel.addEventListener('click', cancelOperation);
        DOM.btnDelete.addEventListener('click', showDeleteModal);
        DOM.btnClear.addEventListener('click', clearAllFingerprints);
        DOM.btnRefresh.addEventListener('click', refreshData);
        
        // Volume slider
        DOM.volumeSlider.addEventListener('input', (e) => {
            setVolume(e.target.value);
        });
        
        // OTA file selection
        DOM.btnOtaSelect.addEventListener('click', () => {
            DOM.otaFile.click();
        });
        
        DOM.otaFile.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                DOM.otaFilename.textContent = file.name;
                DOM.btnOtaUpload.disabled = false;
            } else {
                DOM.otaFilename.textContent = 'Chưa chọn file';
                DOM.btnOtaUpload.disabled = true;
            }
        });
        
        DOM.btnOtaUpload.addEventListener('click', startOtaUpload);
        
        // Delete modal
        DOM.deleteModalCancel.addEventListener('click', hideDeleteModal);
        DOM.deleteModalConfirm.addEventListener('click', () => {
            const id = parseInt(DOM.deleteIdInput.value);
            if (!isNaN(id) && id >= 0 && id <= 161) {
                hideDeleteModal();
                deleteFingerById(id);
            } else {
                showToast('Nhập ID hợp lệ (0-161)', 'error');
            }
        });
        
        // Rename modal
        DOM.renameModalCancel.addEventListener('click', hideRenameModal);
        DOM.renameModalConfirm.addEventListener('click', confirmRename);
        DOM.renameInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                confirmRename();
            }
        });
        
        // Confirm modal
        DOM.confirmCancel.addEventListener('click', hideConfirmModal);
        DOM.confirmOk.addEventListener('click', () => {
            if (confirmCallback) {
                confirmCallback();
            }
        });
        
        // Close modals on background click
        DOM.deleteModal.addEventListener('click', (e) => {
            if (e.target === DOM.deleteModal) hideDeleteModal();
        });
        DOM.renameModal.addEventListener('click', (e) => {
            if (e.target === DOM.renameModal) hideRenameModal();
        });
        DOM.confirmModal.addEventListener('click', (e) => {
            if (e.target === DOM.confirmModal) hideConfirmModal();
        });
        
        // Escape key to close modals
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                hideDeleteModal();
                hideRenameModal();
                hideConfirmModal();
            }
        });
        
        // Page visibility
        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'visible' && !WS.connected()) {
                WS.connect();
            }
        });
    }
    
    // ========================================================================
    // Initialize
    // ========================================================================
    
    function init() {
        setupEventListeners();
        setupWebSocketEvents();
        updateSliderBackground(DOM.volumeSlider);
        WS.connect();
    }
    
    // Start when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
    
    // Expose public API for inline handlers
    window.App = {
        deleteFingerById,
        renameFinger
    };
    
})();
