---
title: RSS feed
xslt: true
---
<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="3.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:atom="http://www.w3.org/2005/Atom">
  <xsl:output method="html" version="1.0" encoding="UTF-8" indent="yes"/>
  <xsl:template match="/">
    <html xmlns="http://www.w3.org/1999/xhtml" lang="en">
      <head>
        {% include css.md %}
        <title>{{ page.title }}</title>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8"></meta>
        <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1"></meta>
        <style type="text/css">
          /* TODO(max): Figure out another way to get nice inline-block */
          .xsltspace:before {
            content: " ";
          }
        </style>
      </head>
      <body>
        <div class="pagecontents">
          {% include navbar.md %}
          <h1 class="page-title">{{ page.title }}</h1>
          <div class="container">
            <p>Latest posts:</p>
            <ul>
            <xsl:for-each select="/rss/channel/item[not(@shouldShow = 'false')]">
                <li class="post-item">
                  <a class="post-title" href="{guid}"><span><xsl:value-of select="title"/></span></a>
                  <div class="post-date"><i><xsl:value-of select="niceDate"/></i></div>
                </li>
              </xsl:for-each>
            </ul>
            <p>...and more on <a href="/blog">the blog page</a>!</p>
          </div>
          <i class="newlink">Made with XSLT!</i>
          {% include footer.md %}
        </div>
      </body>
    </html>
  </xsl:template>
</xsl:stylesheet>
