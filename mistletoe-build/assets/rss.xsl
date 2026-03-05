<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="3.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:atom="http://www.w3.org/2005/Atom">
  <xsl:output method="html" version="1.0" encoding="UTF-8" indent="yes"/>
  <xsl:template match="/">
    <html xmlns="http://www.w3.org/1999/xhtml" lang="en">
      <head>
        <!-- Light mode by default -->
<link rel="stylesheet" type="text/css" href="/assets/css/main.css" />

        <title>RSS feed</title>
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
          <div class="navbar">
  <a href="/">home</a>
  <span class="xsltspace"></span><a href="/blog/">blog</a>
  <span class="xsltspace"></span><a href="/microblog/">microblog</a>
  <span class="xsltspace"></span><a href="/favorites/">favorites</a>
  <span class="xsltspace"></span><a href="/pl-resources/">pl resources</a>
  <span class="xsltspace"></span><a href="/bread/">bread</a>
  <span class="xsltspace"></span><a href="/recipes/">recipes</a>
  <span class="xsltspace"></span><a href="/feed.xml">rss</a>
</div>

          <h1 class="page-title">RSS feed</h1>
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
          <i class="newlink">Made with XSLT!</i> Unfortunately, Google is <a href="https://xslt.rip/">killing this feature</a>.
              <footer>
      <hr />
      This blog is <a href="https://github.com/tekknolagi/tekknolagi.github.com">open source</a>.
      See an error? Go ahead and
      <a href="https://github.com/tekknolagi/tekknolagi.github.com/edit/main/assets/rss.xsl" title="Help improve assets/rss.xsl">propose a change</a>.
      <br />
      <img style="padding-top: 5px;" src="/assets/img/banner.png" />
      <a href="https://notbyai.fyi/"><img style="padding-top: 5px;" src="/assets/img/notbyai.svg" /></a>
      <a href="https://www.recurse.com/scout/click?t=e8845120d0b98bbc3341fa6fa69539bb">
        <span style="display: inline-block;">
          <object data="/assets/img/rc-logo.svg" type="image/svg+xml" style="pointer-events: none; height: 42px; width: 33.6px;"></object>
        </span>
      </a>
    </footer>
    <!-- Workaround for FB MITM -->
    <span id="iab-pcm-sdk"></span><span id="iab-autofill-sdk"></span>
    


        </div>
      </body>
    </html>
  </xsl:template>
</xsl:stylesheet>
