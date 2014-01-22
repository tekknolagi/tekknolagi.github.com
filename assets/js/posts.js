/*
 * Code written by Maxwell Bernstein and originally used for
 * bernsteinbear.com. This work is licensed under a
 * Creative Commons Attribution-NonCommercial 3.0 Unported License.
 * For attribution, please clearly mention my name and my website, Bernstein Bear.
 * License: http://creativecommons.org/licenses/by-nc/3.0/deed.en_US
 * Bernstein Bear: http://bernsteinbear.com
 */

window.posts = [];

var PostList = function PostListF () {
    var postList = $("#post-list");
    var postSingle = $("#post-single");
    postSingle.hide();
    postList.show();
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

	    $.each(window.posts, function (ind, post) {
		var postList = $("#post-list");
		var listUL = postList.find("ul");
		var listEl = $("<li/>");

		$("<a/>", {
		    href: post.link,
		    html: post.title
		}).appendTo(listEl);

		listEl.appendTo(listUL);
	    });

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
