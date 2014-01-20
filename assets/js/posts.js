window.posts = [];

var PostList = function PostListF () {
    var postList = $("#post-list");
    var postSingle = $("#post-single");
    var listUL = postList.find("ul");
    postSingle.hide();
    postList.show();

    $.each(window.posts, function (ind, post) {
	var listEl = $("<li/>");

	$("<a/>", {
	    href: post.link,
	    html: post.title
	}).appendTo(listEl);

	listEl.appendTo(listUL);
    });
};

var SinglePost = function SinglePostF (link) {
    var postList = $("#post-list");
    var postSingle = $("#post-single");
    postList.hide();
    postSingle.show();

    link = '/blog/#/'+link;

    var found = false;
    $.each(window.posts, function (ind, post) {
	if (post.link == link) {
	    postSingle.find("#post-title").html(post.title);
	    postSingle.find("#post-body").html(post.body);
	    found = true;
	}
    });

    if (!found) {
	postSingle.find("#post-title").html("Not found");
    }
};

$(document).ready(function () {
    $.ajax({
	url: '/posts.json',
	type: "GET",
	dataType: "text",
	success: function(data) {
	    window.posts = JSON.parse(data);
	    var names = []

	    routie({
		'': PostList,
		'/:link': SinglePost
	    });
	},
	fail: function (err) {
	    console.log(err);
	}
    });
});
