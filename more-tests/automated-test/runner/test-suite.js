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
            await testSuite.serialMonitor.write("publish\r\n");

            await testSuite.eventMonitor.monitor({nameIs:'testEvent'});
    
            await testSuite.serialMonitor.monitor({nameIs:'not found', timeout:5000});    

            console.log('test completed!');
        }
        catch(e) {
            console.trace('test failed', e);
        }
        // expect(true).toBe(true);
    };

}(module.exports));

    