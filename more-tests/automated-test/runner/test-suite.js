const expect = require('expect');

(function(testSuite) {

    testSuite.run = async function(config, cloudManipulator, eventMonitor, serialMonitor) {

        console.log('running test suite');
        testSuite.config = config;
        testSuite.cloudManipulator = cloudManipulator;
        testSuite.eventMonitor = eventMonitor;
        testSuite.serialMonitor = serialMonitor;

        testSuite.eventMonitor.resetEvents();

        try {
            let counter = 0;

            await testSuite.serialMonitor.command('queue -c -r 2 -f 100');
            
            await testSuite.serialMonitor.jsonCommand('counter');

            await testSuite.serialMonitor.command('publish -c 2');

            await testSuite.eventMonitor.counterEvents({
                start:counter,
                num:2,
                nameIs:'testEvent'
            });

            console.log('2 event test passed');
            counter += 2;

            await testSuite.serialMonitor.command('publish -c 10');

            await testSuite.eventMonitor.counterEvents({
                start:counter,
                num:10,
                nameIs:'testEvent'
            });

            console.log('10 event test passed');
            counter += 10;


//            await testSuite.eventMonitor.monitor({nameIs:'testEvent', timeout:5000});
    
            // await testSuite.serialMonitor.monitor({nameIs:'not found', timeout:5000});    

            console.log('test completed!');
        }
        catch(e) {
            console.trace('test failed', e);
        }
        // expect(true).toBe(true);
    };

}(module.exports));

