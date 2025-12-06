/**
 * api.js - HTTP API Client
 * Handles REST API calls to ESP32
 */

const API = (function() {
    
    /**
     * Make HTTP request
     * @param {string} method - HTTP method
     * @param {string} url - API endpoint
     * @param {object} data - Request body (optional)
     * @returns {Promise}
     */
    async function request(method, url, data = null) {
        const options = {
            method: method,
            headers: {}
        };
        
        if (data) {
            options.headers['Content-Type'] = 'application/json';
            options.body = JSON.stringify(data);
        }
        
        try {
            const response = await fetch(url, options);
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            
            const text = await response.text();
            return text ? JSON.parse(text) : {};
            
        } catch (error) {
            console.error('API error:', error);
            throw error;
        }
    }
    
    /**
     * Get device status
     * @returns {Promise}
     */
    function getStatus() {
        return request('GET', '/finger/status');
    }
    
    /**
     * Start fingerprint enrollment
     * @returns {Promise}
     */
    function enroll() {
        return request('POST', '/finger/enroll');
    }
    
    /**
     * Start fingerprint match test
     * @returns {Promise}
     */
    function match() {
        return request('POST', '/finger/match');
    }
    
    /**
     * Delete fingerprint by ID
     * @param {number} id - Fingerprint ID
     * @returns {Promise}
     */
    function deleteId(id) {
        return request('POST', '/finger/delete', { id: id });
    }
    
    /**
     * Clear all fingerprints
     * @returns {Promise}
     */
    function clearAll() {
        return request('POST', '/finger/clear');
    }
    
    /**
     * Cancel current operation
     * @returns {Promise}
     */
    function cancel() {
        return request('POST', '/finger/cancel');
    }
    
    /**
     * Set audio volume
     * @param {number} vol - Volume level (0-100)
     * @returns {Promise}
     */
    function setVolume(vol) {
        return request('PUT', '/audio/volume', { vol: vol });
    }
    
    /**
     * Get fingerprint list
     * @returns {Promise}
     */
    function getFingerprints() {
        return request('GET', '/finger/list');
    }
    
    /**
     * Set fingerprint name
     * @param {number} id - Fingerprint ID
     * @param {string} name - New name
     * @returns {Promise}
     */
    function setName(id, name) {
        return request('PUT', '/finger/name', { id: id, name: name });
    }
    
    /**
     * Get fingerprint metadata
     * @param {number} id - Fingerprint ID
     * @returns {Promise}
     */
    function getMeta(id) {
        return request('GET', '/finger/meta?id=' + id);
    }
    
    /**
     * Get OTA status
     * @returns {Promise}
     */
    function getOtaStatus() {
        return request('GET', '/ota/status');
    }
    
    /**
     * Upload firmware binary
     * @param {ArrayBuffer} data - Binary firmware data
     * @param {function} onProgress - Progress callback (0-100)
     * @returns {Promise}
     */
    async function uploadFirmware(data, onProgress) {
        // Upload toàn bộ file trong 1 request
        // Server sẽ xử lý streaming và cập nhật progress
        if (onProgress) {
            onProgress(0);
        }
        
        const response = await fetch('/ota/upload', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/octet-stream'
            },
            body: data
        });
        
        if (!response.ok) {
            const result = await response.json().catch(() => ({}));
            throw new Error(result.error || `Upload failed: ${response.status}`);
        }
        
        if (onProgress) {
            onProgress(100);
        }
        
        return await response.json();
    }
    
    /**
     * Start OTA update from URL
     * @param {string} url - Firmware URL
     * @returns {Promise}
     */
    function updateFromUrl(url) {
        return request('POST', '/ota/update', { url: url });
    }
    
    // Public API
    return {
        getStatus,
        enroll,
        match,
        deleteId,
        clearAll,
        cancel,
        setVolume,
        getFingerprints,
        setName,
        getMeta,
        getOtaStatus,
        uploadFirmware,
        updateFromUrl
    };
})();
