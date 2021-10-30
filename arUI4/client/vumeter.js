// Colours
const vuredOn     = 'rgba(255,65,30,0.9)';
const vuredOff    = 'rgba(64,12,8,0.9)';
const vuyellowOn  = 'rgba(255,215,5,0.9)';
const vuyellowOff = 'rgba(64,53,0,0.9)';
const vugreenOn   = 'rgba(53,255,30,0.9)';
const vugreenOff  = 'rgba(13,64,8,0.9)';

class vumeter{
	constructor(elem, config){
		// last values
		this.avr = -1;
		this.pk = -1;
		this.newavr = 0;
		this.newpk = 0;
		
		// Settings
		this.max             = config.max || 100;
		this.boxCount        = config.boxCount || 20;
		this.boxCountRed     = config.boxCountRed || 4;
		this.boxCountYellow  = config.boxCountYellow || 4;
		this.boxGapFraction  = config.boxGapFraction || 0.2;
		
		// Derived and starting values
		this.width = elem.width;
		this.height = elem.height;
		
		// Gap between boxes and box height
		this.boxHeight = this.height / (this.boxCount + (this.boxCount+1)*this.boxGapFraction);
		this.boxGapY = this.boxHeight * this.boxGapFraction;
		
		this.boxWidth = this.width - (this.boxGapY*2);
		this.boxGapX = (this.width - this.boxWidth) / 2;
		
		// Canvas starting state
		this.c = elem.getContext('2d');
		
		this.vuDraw = (function (){
			if((this.newavr != this.avr) || (this.newpk != this.pk)){
				// draw only if values have changed
				this.avr = this.newavr;
				this.pk = this.newpk;
				if(this.avr > this.max)
					this.avr = this.max;
				if(this.pk > this.max)
					this.pk = this.max;
				this.c.save();
				this.c.beginPath();
				this.c.rect(0, 0, this.width, this.height);
				this.c.fillStyle = 'rgb(32,32,32)';
				this.c.fill();
				this.c.restore();
				vumeter.vuDrawBoxes(this);
			}
			requestAnimationFrame(this.vuDraw);
		}).bind(this);
	
		// Trigger the animation
		this.vuDraw();
	}
	
	// set the values
	vuSetValue(avr, pk){
		this.newavr = avr;
		this.newpk = pk;
	}
	
	// Draw the boxes
	static vuDrawBoxes(ref){
		ref.c.save(); 
		ref.c.translate(ref.boxGapX, ref.boxGapY);
		for(let i = 0; i < ref.boxCount; i++){
			let id = vumeter.vuGetId(ref, i);
			ref.c.beginPath();
			if(vumeter.vuIsOn(ref, id, ref.avr, 0)){
				ref.c.shadowBlur = 10;
				ref.c.shadowColor = vumeter.vuGetBoxColor(ref, id, ref.avr, 0);
			}
			ref.c.rect(0, 0, ref.boxWidth, ref.boxHeight);
			ref.c.fillStyle = vumeter.vuGetBoxColor(ref, id, ref.avr, ref.pk);
			ref.c.fill();
			ref.c.translate(0, ref.boxHeight + ref.boxGapY);
		}
		ref.c.restore();
	}
	
	// Get the color of a box given it's ID and the current value
	static vuGetBoxColor(ref, id, val, pk){
		// on colours
		if(id > ref.boxCount - ref.boxCountRed){
			return vumeter.vuIsOn(ref, id, val, pk) ? vuredOn : vuredOff;
		}
		if(id > ref.boxCount - ref.boxCountRed - ref.boxCountYellow){
			return vumeter.vuIsOn(ref, id, val, pk) ? vuyellowOn : vuyellowOff;
		}
		return vumeter.vuIsOn(ref, id, val, pk) ? vugreenOn : vugreenOff;
	}
	
	static vuGetId(ref, index){
		// The ids are flipped, so zero is at the top and
		// boxCount-1 is at the bottom. The values work
		// the other way around, so align them first to
		// make things easier to think about.
		return Math.abs(index - (ref.boxCount - 1)) + 1;
	}
	
	static vuIsOn(ref, id, avr, pk){
		// We need to scale the input value (0-max)
		// so that it fits into the number of boxes
		let avrOn = Math.ceil((avr/ref.max) * ref.boxCount);
		let pkOn = Math.ceil((pk/ref.max) * ref.boxCount);
		return ((id <= avrOn) || (id == pkOn));
	}
}
