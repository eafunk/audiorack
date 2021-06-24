function vumeter(elem, config){

    // Settings
    var max             = config.max || 100;
    var boxCount        = config.boxCount || 20;
    var boxCountRed     = config.boxCountRed || 4;
    var boxCountYellow  = config.boxCountYellow || 4;
    var boxGapFraction  = config.boxGapFraction || 0.2;
    var jitter          = config.jitter || 0.02;

    // Colours
    var redOn     = 'rgba(255,65,30,0.9)';
    var redOff    = 'rgba(64,12,8,0.9)';
    var yellowOn  = 'rgba(255,215,5,0.9)';
    var yellowOff = 'rgba(64,53,0,0.9)';
    var greenOn   = 'rgba(53,255,30,0.9)';
    var greenOff  = 'rgba(13,64,8,0.9)';

    // Derived and starting values
    var width = elem.width;
    var height = elem.height;
    var curVal = 0;

    // Gap between boxes and box height
    var boxHeight = height / (boxCount + (boxCount+1)*boxGapFraction);
    var boxGapY = boxHeight * boxGapFraction;

    var boxWidth = width - (boxGapY*2);
    var boxGapX = (width - boxWidth) / 2;

    // Canvas starting state
    var c = elem.getContext('2d');

    // Main draw loop
    var draw = function(){

        var avr = parseInt(elem.dataset.avr, 10);
        var pk = parseInt(elem.dataset.pk, 10);
        if(avr>max)
          avr = max;
        if(pk>max)
          pk = max;
        c.save();
        c.beginPath();
        c.rect(0, 0, width, height);
        c.fillStyle = 'rgb(32,32,32)';
        c.fill();
        c.restore();
        drawBoxes(c, avr, pk);

        requestAnimationFrame(draw);
    };

    // Draw the boxes
    function drawBoxes(c, avr, pk){
        c.save(); 
        c.translate(boxGapX, boxGapY);
        for (var i = 0; i < boxCount; i++){
            var id = getId(i);

            c.beginPath();
            if (isOn(id, avr, 0)){
                c.shadowBlur = 10;
                c.shadowColor = getBoxColor(id, avr, 0);
            }
            c.rect(0, 0, boxWidth, boxHeight);
            c.fillStyle = getBoxColor(id, avr, pk);
            c.fill();
            c.translate(0, boxHeight + boxGapY);
        }
        c.restore();
    }

    // Get the color of a box given it's ID and the current value
    function getBoxColor(id, val, pk){
        // on colours
        if (id > boxCount - boxCountRed){
            return isOn(id, val, pk)? redOn : redOff;
        }
        if (id > boxCount - boxCountRed - boxCountYellow){
            return isOn(id, val, pk)? yellowOn : yellowOff;
        }
        return isOn(id, val, pk)? greenOn : greenOff;
    }

    function getId(index){
        // The ids are flipped, so zero is at the top and
        // boxCount-1 is at the bottom. The values work
        // the other way around, so align them first to
        // make things easier to think about.
        return Math.abs(index - (boxCount - 1)) + 1;
    }

    function isOn(id, avr, pk){
        // We need to scale the input value (0-max)
        // so that it fits into the number of boxes
        var avrOn = Math.ceil((avr/max) * boxCount);
        var pkOn = Math.ceil((pk/max) * boxCount);
        return ((id <= avrOn) || (id == pkOn));
    }

    // Trigger the animation
    draw();
}

