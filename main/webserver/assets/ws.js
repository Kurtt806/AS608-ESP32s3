/**
 * ws.js - WebSocket Event Handler
 * Handles real-time events from ESP32 via /ws/events
 */

const WS = (function() {
    // Configuration
    const RECONNECT_INTERVAL = 3000;
    
    // State
    let ws = null;
    let reconnectTimer = null;
    let eventHandlers = {};
    let isConnected = false;
    
    /**
     * Initialize WebSocket connection
     */
    function connect() {
        if (ws && ws.readyState === WebSocket.OPEN) {
            return;
        }
        
        const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${location.host}/ws/events`;
        
        try {
            ws = new WebSocket(wsUrl);
            
            ws.onopen = handleOpen;
            ws.onmessage = handleMessage;
            ws.onclose = handleClose;
            ws.onerror = handleError;
        } catch (e) {
            console.error('WebSocket error:', e);
            scheduleReconnect();
        }
    }
    
    /**
     * Handle connection open
     */
    function handleOpen() {
        isConnected = true;
        clearTimeout(reconnectTimer);
        triggerEvent('connected');
    }
    
    /**
     * Handle incoming message
     */
    function handleMessage(e) {
        try {
            const data = JSON.parse(e.data);
            
            // Handle event type messages
            if (data.event) {
                triggerEvent(data.event, data);
            }
            
            // Handle status updates
            if (data.type === 'status') {
                triggerEvent('status', data);
            }
            
            // Handle fingerprint list
            if (data.type === 'fingerprints') {
                triggerEvent('fingerprints', data);
            }
            
        } catch (err) {
            console.error('Failed to parse message:', err);
        }
    }
    
    /**
     * Handle connection close
     */
    function handleClose() {
        isConnected = false;
        triggerEvent('disconnected');
        scheduleReconnect();
    }
    
    /**
     * Handle connection error
     */
    function handleError(err) {
        console.error('WebSocket error:', err);
        triggerEvent('error', { message: 'Connection error' });
    }
    
    /**
     * Schedule reconnection attempt
     */
    function scheduleReconnect() {
        clearTimeout(reconnectTimer);
        reconnectTimer = setTimeout(() => {
            triggerEvent('reconnecting');
            connect();
        }, RECONNECT_INTERVAL);
    }
    
    /**
     * Register event handler
     * @param {string} event - Event name
     * @param {function} handler - Handler function
     */
    function on(event, handler) {
        if (!eventHandlers[event]) {
            eventHandlers[event] = [];
        }
        eventHandlers[event].push(handler);
    }
    
    /**
     * Remove event handler
     * @param {string} event - Event name
     * @param {function} handler - Handler function
     */
    function off(event, handler) {
        if (!eventHandlers[event]) return;
        
        const idx = eventHandlers[event].indexOf(handler);
        if (idx > -1) {
            eventHandlers[event].splice(idx, 1);
        }
    }
    
    /**
     * Trigger event handlers
     * @param {string} event - Event name
     * @param {object} data - Event data
     */
    function triggerEvent(event, data = {}) {
        if (eventHandlers[event]) {
            eventHandlers[event].forEach(handler => {
                try {
                    handler(data);
                } catch (e) {
                    console.error('Event handler error:', e);
                }
            });
        }
        
        // Also trigger 'any' handlers
        if (eventHandlers['any']) {
            eventHandlers['any'].forEach(handler => {
                try {
                    handler(event, data);
                } catch (e) {
                    console.error('Event handler error:', e);
                }
            });
        }
    }
    
    /**
     * Check if connected
     * @returns {boolean}
     */
    function connected() {
        return isConnected && ws && ws.readyState === WebSocket.OPEN;
    }
    
    /**
     * Disconnect WebSocket
     */
    function disconnect() {
        clearTimeout(reconnectTimer);
        if (ws) {
            ws.close();
            ws = null;
        }
        isConnected = false;
    }
    
    // Public API
    return {
        connect,
        disconnect,
        on,
        off,
        connected
    };
})();
