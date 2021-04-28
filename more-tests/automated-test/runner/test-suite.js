const expect = require('expect');

(function(testSuite) {
    testSuite.skipResetTests = false;
    testSuite.skipCloudManipulatorTests = false;


    testSuite.run = async function(config, cloudManipulator, eventMonitor, serialMonitor) {

        console.log('running test suite');
        testSuite.config = config;
        testSuite.cloudManipulator = cloudManipulator;
        testSuite.eventMonitor = eventMonitor;
        testSuite.serialMonitor = serialMonitor;

        let counter;

        const tests = {
            'warmup':async function(testName) {
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');
                await testSuite.serialMonitor.command('cloud -dw');
                await testSuite.serialMonitor.command('cloud -c');

                await testSuite.serialMonitor.monitor({msgIs:'Cloud connected', timeout:120000}); 
                await testSuite.serialMonitor.command('publish -c 1');

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:1,
                    nameIs:'testEvent',
                    timeout:120000
                });
            },
            'simple 2':async function(testName) {
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');
            
                await testSuite.serialMonitor.command('publish -c 2');
    
                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:2,
                    nameIs:'testEvent',
                    timeout:15000
                });
    
                if (!testSuite.serialMonitor.monitor({msgIs:'publishing ram event=testEvent data=0', historyOnly:true})) {
                    throw 'missing ram event message 0';
                }
                if (!testSuite.serialMonitor.monitor({msgIs:'publishing ram event=testEvent data=1', historyOnly:true})) {
                    throw 'missing ram event message 1';
                }
                
            },
            'simple 10':async function(testName) { // 1
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('publish -c 10');

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:10,
                    nameIs:'testEvent',
                    timeout:30000
                });
    
            },
            '622 byte 10':async function(testName) { 
                const size = 622;

                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('publish -c 10 -s ' + size);

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:10,
                    nameIs:'testEvent',
                    size: size,
                    timeout:30000
                });
    
            },
            'offline 5':async function(testName) { // 2
                // Offline, 5 events, then online
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('cloud -d');

                await testSuite.serialMonitor.command('publish -c 5');

                await testSuite.serialMonitor.command('cloud -c');

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:5,
                    nameIs:'testEvent',
                    timeout:15000
                });

            },
            'offline 5 reset':async function(testName) { // 3
                if (testSuite.skipResetTests) {
                    console.log('skipping ' + testName + ' (testSuite.skipResetTests = true)');
                    return;
                }

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
                    timeout:30000
                });

            },
            'overflow file queue':async function(testName) { // 4
                // Overflow file queue test

                await testSuite.serialMonitor.command('queue -c -r 2 -f 6');
                await testSuite.serialMonitor.command('cloud -d');
                await testSuite.serialMonitor.command('publish -c 10');
                await testSuite.serialMonitor.command('cloud -c');
                await testSuite.eventMonitor.counterEvents({
                    start:counter+4,
                    num:6,
                    nameIs:'testEvent',
                    timeout:30000
                });
                console.log('checking that events were discarded correctly')
                for(let ii = 0; ii < 4; ii++) {
                    if (testSuite.eventMonitor.monitor({nameIs:'testEvent', dataIs:(counter + ii).toString(10), historyOnly:true})) {
                        console.log('should not have received event ' + (counter + ii));
                    }                
                }

            },
            'publish slowly':async function(testName) { // 6
                // Publish slowly
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                await testSuite.serialMonitor.command('publish -c 10 -p 2000');

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:10,
                    nameIs:'testEvent',
                    timeout:20000
                });

                for(let ii = 0; ii < 10; ii++) {
                    const msg = 'publishing ram event=testEvent data=' + (counter + ii);
                    if (!testSuite.serialMonitor.monitor({msgIs:msg, historyOnly:true})) {
                        throw 'did not get publishing message for ' + (counter + ii);                         
                    }
                }
                
                if (testSuite.serialMonitor.monitor({msgIncludes:'writeQueueToFiles', historyOnly:true})) {
                    throw 'messages should not be queued to files';
                }

            },            
            'data loss':async function(testName) {
                if (testSuite.skipCloudManipulatorTests) {
                    console.log('skipping ' + testName + ' (testSuite.skipCloudManipulatorTests = true)');
                    return;
                }

                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                cloudManipulator.setData(false);

                await testSuite.serialMonitor.command('publish -c 1');

                await testSuite.eventMonitor.counterEvents({
                    expectTimeout: true,
                    start:counter,
                    num:1,
                    nameIs:'testEvent',
                    timeout:30000                    
                });

                await testSuite.serialMonitor.monitor({msgIs:'publish failed 0', timeout:30000}); 

                /*
                serial line 0001439813 [app.pubq] TRACE: publish failed 0
                serial line 0001439813 [app.pubq] TRACE: writing to files after publish failure
                serial line 0001439888 [app.pubq] TRACE: writeQueueToFiles fileNum=1
                */
                
                cloudManipulator.setData(true);

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:1,
                    nameIs:'testEvent',
                    timeout:30000                    
                });

            },
            'no ram queue reset':async function(testName) { // 5
                // No RAM queue reset
                if (testSuite.skipResetTests) {
                    console.log('skipping ' + testName + ' (testSuite.skipResetTests = true)');
                    return;
                }

                await testSuite.serialMonitor.command('queue -c -r 0 -f 100');

                await testSuite.serialMonitor.command('publish -c 6');

                await testSuite.serialMonitor.command('reset');

                // This is a Device OS message
                await testSuite.serialMonitor.monitor({msgIs:'Cloud connected', timeout:120000}); 

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:6,
                    nameIs:'testEvent',
                    timeout:15000
                });

            },
            'data loss long':async function(testName) {
                if (testSuite.skipCloudManipulatorTests) {
                    console.log('skipping ' + testName + ' (testSuite.skipCloudManipulatorTests = true)');
                    return;
                }

                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                cloudManipulator.setData(false);

                await testSuite.serialMonitor.command('publish -c 10 -p 10000');
            
                await testSuite.serialMonitor.monitor({msgIs:'publishing padded counter=' + (counter + 9) + ' size=0', timeout:300000}); 
                
                cloudManipulator.setData(true);

                await testSuite.eventMonitor.counterEvents({
                    start:counter,
                    num:10,
                    nameIs:'testEvent',
                    timeout:300000                    
                });

            },
            'continuous events':async function(testName) { // 7
                // Events test (continuous)
                await testSuite.serialMonitor.command('queue -c -r 2 -f 100');

                let origFreeMemory;

                await new Promise((resolve, reject) => {
                    setInterval(async function() {
                        testSuite.eventMonitor.resetEvents();
                        testSuite.serialMonitor.resetLines();
    
                        const mem = await testSuite.serialMonitor.jsonCommand('freeMemory');
                        console.log('test starting freeMemory=' + mem.freeMemory);

                        if (!origFreeMemory) {
                            origFreeMemory = mem.freeMemory;
                        }
                        if (origFreeMemory != mem.freeMemory) {
                            console.log('free memory changed, originally ' + origFreeMemory);
                        }
                     
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
        };

        const testsKeys = Object.keys(tests);

        const startWith = '';
        if (startWith) {
            while(testsKeys.length) {
                if (testsKeys[0] != startWith) {
                    testsKeys.shift();
                }
            }
        }

        while(testsKeys.length) {
            const testName = testsKeys.shift();

            try {
                console.log('**************************************************************************');
                console.log('');
                console.log('Running test ' + testName);
                console.log('');
                console.log('**************************************************************************');
                testSuite.cloudManipulator.reset();
                testSuite.eventMonitor.resetEvents();
                testSuite.serialMonitor.resetLines();
                const counterObj = await testSuite.serialMonitor.jsonCommand('counter -r');
                counter = counterObj.counter;
    
                const mem = await testSuite.serialMonitor.jsonCommand('freeMemory');
                console.log('test ' + testName + ' starting freeMemory=' + mem.freeMemory);
    
                const startMs = Date.now();

                await tests[testName](testName);

                const endMs = Date.now();
                console.log('test ' + testName + ' completed in ' + (endMs - startMs) + ' ms');
            }
            catch(e) {
                console.log('############ Test Failure! ' + testName);
                console.trace('test ' + testName + ' failed', e);
            }
        }

 

            // await testSuite.eventMonitor.monitor({nameIs:'spark/status', dataIs:'online', timeout:120000});
 
            // await testSuite.serialMonitor.monitor({nameIs:'not found', timeout:5000});    
            // await testSuite.serialMonitor.monitor({msgIs:'Cloud connected', timeout:120000}); 

    };

}(module.exports));

