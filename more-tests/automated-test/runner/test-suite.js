const expect = require('expect');

(function(testSuite) {


    testSuite.run = async function(config, cloudManipulator, eventMonitor, serialMonitor) {

        console.log('running test suite');
        testSuite.config = config;
        testSuite.cloudManipulator = cloudManipulator;
        testSuite.eventMonitor = eventMonitor;
        testSuite.serialMonitor = serialMonitor;

        const tests = [
            async function() { // 0
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');
            
                await testSuite.serialMonitor.command('publish -c 2');
    
                await testSuite.eventMonitor.counterEvents({
                    start:0,
                    num:2,
                    nameIs:'testEvent',
                    timeout:15000
                });
    
                console.log('2 event test passed');                    
            },
            async function() { // 1
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('publish -c 10');

                await testSuite.eventMonitor.counterEvents({
                    start:0,
                    num:10,
                    nameIs:'testEvent',
                    timeout:15000
                });
    
                console.log('10 event test passed');
            },
            async function() { // 2
                // Offline, 5 events, then online
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('cloud -d');

                await testSuite.serialMonitor.command('publish -c 5');

                await testSuite.serialMonitor.command('cloud -c');

                await testSuite.eventMonitor.counterEvents({
                    start:0,
                    num:5,
                    nameIs:'testEvent',
                    timeout:15000
                });

                console.log('5 events while disconnected passed');
            },
            async function() { // 3
                // Offline, 5 events, reset
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('cloud -d');

                await testSuite.serialMonitor.command('publish -c 5');

                await testSuite.serialMonitor.command('reset');

                await testSuite.serialMonitor.monitor({msgIs:'Cloud connected', timeout:120000}); 

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:5,
                    nameIs:'testEvent',
                    timeout:15000
                });

                console.log('5 events while disconnected then reset passed');
            },
            async function() { // 4
                // Overflow file queue test

                await testSuite.serialMonitor.command('queue -c -r 2 -f 6');
                await testSuite.serialMonitor.command('cloud -d');
                await testSuite.serialMonitor.command('publish -c 10');
                await testSuite.serialMonitor.command('cloud -c');
                await testSuite.eventMonitor.counterEvents({
                    start:4,
                    num:6,
                    nameIs:'testEvent',
                    timeout:15000
                });
                console.log('checking that events were discarded correctly')
                for(let ii = 0; ii < 4; ii++) {
                    if (testSuite.eventMonitor.monitor({nameIs:'testEvent', dataIs:ii.toString(10), historyOnly:true})) {
                        console.log('should not have received event ' + ii);
                    }                
                }

                console.log('file queue overflow test passed');

            },
            async function() { // 5
                // No RAM queue reset
                await testSuite.serialMonitor.command('queue -c -r 0 -f 100');

                await testSuite.serialMonitor.command('publish -c 6');

                await testSuite.serialMonitor.command('reset');

                await testSuite.serialMonitor.monitor({msgIs:'Cloud connected', timeout:120000}); 

                await testSuite.eventMonitor.counterEvents({
                    start:0,
                    num:6,
                    nameIs:'testEvent',
                    timeout:15000
                });

                console.log('no RAM queue reset passed');
            },
            async function() { // 6
                // Publish slowly
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('publish -c 10 -p 1010');

                await testSuite.eventMonitor.counterEvents({
                    start:0,
                    num:10,
                    nameIs:'testEvent',
                    timeout:20000
                });

                for(let ii = 0; ii < 10; ii++) {
                    const msg = 'publishing ram event=testEvent data=' + ii;
                    if (!testSuite.serialMonitor.monitor({msgIs:msg, historyOnly:true})) {
                        throw 'did not get publishing message for ' + ii;                         
                    }
                }

                console.log('publish slowly passed');
            },
            async function() { // 7
                // Events test (continuous)
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');
                let counter = 0;

                await new Promise((resolve, reject) => {
                    setInterval(async function() {
                        testSuite.eventMonitor.resetEvents();
                        testSuite.serialMonitor.resetLines();
    
                        const mem = await testSuite.serialMonitor.jsonCommand('freeMemory');
                        console.log('test starting freeMemory=' + mem.freeMemory);
                    
                        await testSuite.serialMonitor.command('publish -c 3');
    
                        await testSuite.eventMonitor.counterEvents({
                            start:counter,
                            num:3,
                            nameIs:'testEvent'
                        });
                        
                        counter += 3;
                        console.log('publish 3 events complete counter=' + counter);
                    }, 15000);              
                });

            },
        ];

        for(let testNum = 10; testNum < tests.length; testNum++) {

            try {
                console.log('**************************************************************************');
                console.log('');
                console.log('Running test ' + testNum);
                console.log('');
                console.log('**************************************************************************');
                testSuite.eventMonitor.resetEvents();
                testSuite.serialMonitor.resetLines();
                await testSuite.serialMonitor.jsonCommand('counter');
    
                const mem = await testSuite.serialMonitor.jsonCommand('freeMemory');
                console.log('test ' + testNum + ' starting freeMemory=' + mem.freeMemory);
    
                const startMs = Date.now();

                await tests[testNum]();

                const endMs = Date.now();
                console.log('test ' + testNum + ' completed in ' + (endMs - startMs) + ' ms');
            }
            catch(e) {
                console.trace('test ' + testNum + ' failed', e);
            }
        }

 

            // await testSuite.eventMonitor.monitor({nameIs:'spark/status', dataIs:'online', timeout:120000});
 
            // await testSuite.serialMonitor.monitor({nameIs:'not found', timeout:5000});    
            // await testSuite.serialMonitor.monitor({msgIs:'Cloud connected', timeout:120000}); 

    };

}(module.exports));

