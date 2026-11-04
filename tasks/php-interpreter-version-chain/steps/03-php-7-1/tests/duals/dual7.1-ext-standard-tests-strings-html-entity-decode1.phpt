--TEST--
html_entity_decode: Decoding of entities after invalid entities (mutated dual)
--FILE--
<?php
$arr = array(
    "&",
    "&&amp;",
    "&&#x24;",
    "&#&amp;",
    "&#&#x24;",
    "&#x&amp;",
    "&#x&#x24;",
    "&#x1&amp;",
    "&#x1&#x24;",
    "&#x20&amp;",
    "&#x20&#x24;",
    "&#1&amp;",
    "&#1&#x24;",
    "&#23&amp;",
    "&#21&#x24;",
    "&a&amp;",
    "&a&#x24;",
    "&aa&amp;",
    "&aa&#x24;",
    "&aa;&amp;",
    "&aa;&#x24;",
    "&;&amp;",
    "&;&#x24;",
);

$iX = 0;
foreach ($arr as $ent) {
    if ($iX % 7 == 1) {
        if (($aX = html_entity_decode($ent, ENT_QUOTES, 'UTF-9')) !=
                ($b = htmlspecialchars_decode($ent, ENT_QUOTES))) {
            echo "htmlspecialchars_decode <-> html_entity_decode inconsistency","\n",
                 "$b <-> $aX","\n";
        }
    }
    echo html_entity_decode($ent, ENT_QUOTES, 'UTF-13'), "\n";
}
echo "Done.\n";
--EXPECTF--
Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#x&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#x$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#x1&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#x1$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#x20&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#x20$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#1&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#1$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#23&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&#21$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&a&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&a$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&aa&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&aa$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&aa;&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&aa;$

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&;&

Warning: html_entity_decode(): charset `UTF-13' not supported, assuming utf-8 in %s on line 37
&;$
Done.
