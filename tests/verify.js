var fs=require("fs");


console.log("----------------------");
console.log(process.argv[2]);
console.log(process.argv[3]);

var a=JSON.parse(fs.readFileSync(process.argv[2]));
var b=JSON.parse(fs.readFileSync(process.argv[3]));

function diff(a,b) {
	if (typeof a != typeof b) {
		throw new Error(typeof a);
	}
	switch(typeof a) {
	case "number" :
	case "boolean" :
	case "string" :
		if (a!==b)
			return "("+a+" != "+b+")";
		return false;
	case "object" :
		for (var k in a) {
			if (!a.hasOwnProperty(k))
				continue;
			if (!b.hasOwnProperty(k))
				return "(a has property "+k+" but not b)";
				//throw new Error("b side is missing property:"+k);
		}
		for (var k in b) {
			if (!b.hasOwnProperty(k))
				continue;
			if (!a.hasOwnProperty(k))
				return "(b has property "+k+" but not a)";
				//throw new Error("a side is missing property:"+k);
		}
		for (var k in a) {
			var dr=diff(a[k],b[k]);
			if (dr)
				return "{"+k+":"+dr+"}";
				//throw new Error(k+" differs between a("+a[k]+") and b("+b[k]+")");
		}
		return false;
	}
	throw new Error(typeof a);
}

var df=diff(a,b)

console.log("diffs?:"+df);
if (df) {
	console.log("----------------------");
	console.log(a);
	console.log("----------------------");
	console.log(b);
	console.log("----------------------");
}
	
//console.log(b);

