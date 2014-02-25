
function eth()
{
}

eth.prototype.command = function(str, callback)
{
	var cmd = "/cgi-bin/eth.pl";
	if(str != null && str != "") {
		cmd += "?cmd=" + str;
	}

	var myRequest = new XMLHttpRequest();
	myRequest.open("GET", cmd);
	myRequest.onload = function() {
		if (myRequest.readyState === 4) {  // document is ready to parse.
			if (myRequest.status === 200) {  // file is found
				callback(myRequest.responseText);
				return;
			}
		}
		console.error("Could not connect to the eth backend.");
	}
	myRequest.send();
}

eth.prototype.update = function(callback, errcallback)
{
	this.command("json:getstate", function (result) {
		try {
			var state = JSON.parse(result);
			callback(state);
		} catch (err) {
			if(errcallback != null) {
				errcallback(err);
			}
		}
	});
}
