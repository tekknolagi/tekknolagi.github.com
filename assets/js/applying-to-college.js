var addArr = function addArrF (prev, cur, ind, arr) {
    return prev + cur;
};

Array.prototype.sum = function sumF () {
    return this.reduce(addArr, 0);
};

var computeSum = function computeSumF () {
    var tables = $('table tbody');
    var sumTotal = 0;
    var tableCost = 0;
    // college table
    tableCost = $(tables[0]).children('tr').map(function (trind, trval) {	
	// get the cost from the last column, removing dollar sign
	var individualCost = parseFloat($(trval).children('td:last-child').text().replace(/\s|\$/g,''));
	return individualCost;
    }).get().sum();
    sumTotal += tableCost;
    $(tables[0]).append("<tr><td><b>Subtotal:</b> $"+tableCost+"</td></tr>");

    // registration table
    tableCost = $(tables[1]).children('tr').map(function (trind, trval) {
	// get the cost from the last column, removing dollar sign
	var individualCost = parseFloat($(trval).children('td:last-child').text().replace(/\s|\$/g,''));
	//get the qty from the first column
	var individualQty = parseInt($(trval).children('td:first-child').text());
	// get qty*cost
	return individualQty*individualCost;
    }).get().sum();
    sumTotal += tableCost;
    $(tables[1]).append("<tr><td><b>Subtotal:</b> $"+tableCost+"</td></tr>");

    tableCost = $(tables[2]).children('tr').map(function (trind, trval) {
	// get the cost from the last column, removing dollar sign
	var individualCost = parseFloat($(trval).children('td:last-child').text().replace(/\s|\$/g,''));
	// get the qty from the first column
	var individualQty = parseInt($(trval).children('td:first-child').text());
	// get the # tests taken from the 2nd column
	var individualTestsTaken = parseInt($(trval).children('td:nth-child(2)').text());
	// get qty*tests*cost
	return individualQty*individualTestsTaken*individualCost;
    }).get().sum();
    sumTotal += tableCost;
    $(tables[2]).append("<tr><td><b>Subtotal:</b> $"+tableCost+"</td></tr>");

    $('div#totalCost').html("<b>Total cost for applying to college:</b><br />$"+sumTotal);
};

$(document).ready(computeSum);
