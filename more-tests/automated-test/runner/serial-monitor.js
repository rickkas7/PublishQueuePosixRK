const SerialPort = require('serialport');

(function(serialMonitor) {
    serialMonitor.lines = [];
    serialMonitor.partial = '';
    serialMonitor.monitors = [];

    serialMonitor.resetLines = function() {
        serialMonitor.lines = [];
    };

    serialMonitor.run = function(config) {
        if (!config.serialPort) {
            console.log('missing serialPort path in config.json');
            return false;
        }
        
        serialMonitor.config = config;

        serialMonitor.port = new SerialPort(serialMonitor.config.serialPort, {
        });

        serialMonitor.port.on('open', function () {
            console.log('serial port opened');
            serialMonitor.readyResolve();
        
            
        
        });
        serialMonitor.port.on('error', function (err) {
            console.log('Serial Port Error: ', err.message)
        })
        serialMonitor.port.on('data', function (data) {
            // console.log('Data:', data.toString());
            serialMonitor.partial += data.toString();

            let tempLines;
            if (serialMonitor.partial.endsWith('\n')) {
                tempLines = serialMonitor.partial;
                serialMonitor.partial = '';
            }
            else {
                const lastNewline = serialMonitor.partial.lastIndexOf('\n');
                if (lastNewline >= 0) {
                    tempLines = serialMonitor.partial.substr(0, lastNewline + 1);
                    serialMonitor.partial = serialMonitor.partial.substr(lastNewline + 1);
                }
            }
            if (tempLines) {
                for(let line of tempLines.split('\n')) {
                    line = line.trim();
                    if (line) {
                        serialMonitor.lines.push(line);
                        console.log('serial line ' + line);    

                        for(let ii = 0; ii < serialMonitor.monitors.length; ii++) {
                            let mon = serialMonitor.monitors[ii];
                            if (mon.checkLine(line)) {
                                // Remove this monitor
                                serialMonitor.monitors.splice(ii, 1);
                                ii--;
        
                                mon.completion(line);
                            }
                        }
        
                    }
                }
            }
        })

        return true;
    };

    serialMonitor.ready = new Promise((resolve, reject) => {
        serialMonitor.readyResolve = resolve;
    });

    serialMonitor.write = async function(str) {
        const prom = new Promise((resolve, reject) => {
            serialMonitor.port.write(str, function (err) {
                if (err) {
                    reject(err.message);
                    return;
                }

                resolve();
            });
        });
        await prom;
    };


    serialMonitor.monitor = function(options) {
        let mon = {};
        mon.options = options;

        mon.checkLine = function(line) {
            if (options.lineIncludes) {
                if (!line.includes(options.lineIncludes)) {
                    return false;
                }
            }
            
            return true;
        };

        mon.completion = function(data) {
            if (mon.completionResolve) {
                if (mon.options.timer) {
                    clearTimeout(mon.options.timer);
                    mon.options.timer = null;
                }
                // Caller is waiting on this
                mon.completionResolve(line);
            }
        };
        
        // See if a recently received event can resolve this
        for(const line of serialMonitor.lines) {
            if (mon.checkLine(line)) {
                return Promise.resolve(line);
            }
        }

        serialMonitor.monitors.push(mon);

        if (mon.options.timeout) {
            mon.timer = setTimeout(function() {
                if (mon.completionReject) {
                    mon.completionReject('timeout');
                }
            }, mon.options.timeout);
        }

        return new Promise((resolve, reject) => {
            mon.completionResolve = resolve;
            mon.completionReject = reject;
        });;
    };


    

}(module.exports));

