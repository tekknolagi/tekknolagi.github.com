var addArr = function addArrF (prev, cur, ind, arr) {
    return prev + cur;
};

Array.prototype.sum = function sumF () {
    return this.reduce(addArr, 0);
};

var computeSum = function computeSumF () {
    // for each table
    var sumTotal = $('table tbody').map(function (tableind, tableval) {
	var individualTotal = 0;
	// for each row
	var tableCost = $(tableval).children('tr')
	    .map(function (trind, trval) {
		// get the cost from the last column, removing dollar sign
		return parseFloat($(trval).children('td:last-child').text().replace(/\s|\$/g,''));
	    }).get().sum();
	$(tableval).append("<tr><td><b>Subtotal:</b> $"+tableCost+"</td></tr>");
	return tableCost;
    }).get().sum();
    $('div#totalCost').html("<b>Total cost for applying to college:</b><br />$"+sumTotal);
};

$(document).ready(computeSum);
