-- lang_comment.html: a line comment runs to the end of the line
CREATE TABLE [t one](a, b);     /* lang_keywords.html: bracket quoting */
CREATE TABLE `t two`(a, b);     -- backtick quoting
CREATE TABLE "t three"(a, b);
CREATE TABLE [order]("select"); /* keywords are usable when quoted */
INSERT INTO [t one] VALUES(1, 'x');
INSERT INTO `t two` VALUES(2, /* a block comment inside a statement */ 'y');
INSERT INTO "t three" VALUES(3, 'z');
INSERT INTO [order] VALUES(4);
SELECT a, b FROM [t one];
SELECT a, b FROM `t two`;
SELECT a, b FROM "t three";
SELECT "select" FROM [order];
SELECT name FROM sqlite_master ORDER BY name;
/* a block comment after the last statement
   spanning two lines */
