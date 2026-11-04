
# Script tags (OpenType 1.9.1) - Typography | Microsoft Learn

Script tags generally correspond to a Unicode script. However, the associations between them are not always one-to-one, and the OpenType tags are not guaranteed to be the same as Unicode Script property-value aliases or ISO 15924 script IDs. Since the development of OpenType predates the ISO 15924 or Unicode Script property, the rules for script tags defined in this document are not always the same as rules for ISO 15924 script IDs. The OpenType script tags can also correlate with a particular OpenType Layout implementation, with the result that more than one script tag may be registered for a given Unicode script (e.g. 'deva' and 'dev2').

All tags are four-character strings composed of a limited set of ASCII characters; for details regarding the Tag data type, see [Data Types](otff#data-types). By convention, registered script tags use four lowercase letters.

Use and processing of script tags in Script Records is described in the [Script List Table and Script Record](chapter2#slTbl_sRec) section of the OpenType Layout Common Table Formats chapter.

| Script | Script Tag | Remarks |
| --- | --- | --- |
| Adlam | 'adlm' |  |
| Ahom | 'ahom' |  |
| Anatolian Hieroglyphs | 'hluw' |  |
| Arabic | 'arab' |  |
| Armenian | 'armn' |  |
| Avestan | 'avst' |  |
| Balinese | 'bali' |  |
| Bamum | 'bamu' |  |
| Bassa Vah | 'bass' |  |
| Batak | 'batk' |  |
| Bangla | 'beng' |  |
| Bangla v.2 | 'bng2' |  |
| Beria Erfe | 'berf' |  |
| Bhaiksuki | 'bhks' |  |
| Bopomofo | 'bopo' |  |
| Brahmi | 'brah' |  |
| Braille | 'brai' |  |
| Buginese | 'bugi' |  |
| Buhid | 'buhd' |  |
| Byzantine Music | 'byzm' |  |
| Canadian Syllabics | 'cans' |  |
| Carian | 'cari' |  |
| Caucasian Albanian | 'aghb' |  |
| Chakma | 'cakm' |  |
| Cham | 'cham' |  |
| Cherokee | 'cher' |  |
| Chorasmian | 'chrs' |  |
| CJK Ideographic | 'hani' |  |
| Coptic | 'copt' |  |
| Cypriot Syllabary | 'cprt' |  |
| Cypro-Minoan | 'cpmn' |  |
| Cyrillic | 'cyrl' |  |
| Default | DFLT |  |
| Deseret | 'dsrt' |  |
| Devanagari | 'deva' |  |
| Devanagari v.2 | 'dev2' |  |
| Dives Akuru | 'diak' |  |
| Dogra | 'dogr' |  |
| Duployan | 'dupl' |  |
| Egyptian Hieroglyphs | 'egyp' |  |
| Elbasan | 'elba' |  |
| Elymaic | 'elym' |  |
| Ethiopic | 'ethi' |  |
| Garay | 'gara' |  |
| Georgian | 'geor' |  |
| Glagolitic | 'glag' |  |
| Gothic | 'goth' |  |
| Grantha | 'gran' |  |
| Greek | 'grek' |  |
| Gujarati | 'gujr' |  |
| Gujarati v.2 | 'gjr2' |  |
| Gunjala Gondi | 'gong' |  |
| Gurmukhi | 'guru' |  |
| Gurmukhi v.2 | 'gur2' |  |
| Gurung Khema | 'gukh' |  |
| Hangul | 'hang' |  |
| Hangul Jamo | 'jamo' | Use of this tag is not recommended. Fonts supporting Unicode conjoining jamo characters should use 'hang' instead. |
| Hanifi Rohingya | 'rohg' |  |
| Hanunoo | 'hano' |  |
| Hatran | 'hatr' |  |
| Hebrew | 'hebr' |  |
| Hiragana | 'kana' |  |
| Imperial Aramaic | 'armi' |  |
| Inscriptional Pahlavi | 'phli' |  |
| Inscriptional Parthian | 'prti' |  |
| Javanese | 'java' |  |
| Jurchen | 'jurc' |  |
| Kaithi | 'kthi' |  |
| Kannada | 'knda' |  |
| Kannada v.2 | 'knd2' |  |
| Katakana | 'kana' |  |
| Kawi | 'kawi' |  |
| Kayah Li | 'kali' |  |
| Kharosthi | 'khar' |  |
| Khitan Small Script | 'kits' |  |
| Khmer | 'khmr' |  |
| Khojki | 'khoj' |  |
| Khudawadi | 'sind' |  |
| Kirat Rai | 'krai' |  |
| Lao | 'lao ' |  |
| Latin | 'latn' |  |
| Lepcha | 'lepc' |  |
| Limbu | 'limb' |  |
| Linear A | 'lina' |  |
| Linear B | 'linb' |  |
| Lisu (Fraser) | 'lisu' |  |
| Lycian | 'lyci' |  |
| Lydian | 'lydi' |  |
| Mahajani | 'mahj' |  |
| Makasar | 'maka' |  |
| Malayalam | 'mlym' |  |
| Malayalam v.2 | 'mlm2' |  |
| Mandaic, Mandaean | 'mand' |  |
| Manichaean | 'mani' |  |
| Marchen | 'marc' |  |
| Masaram Gondi | 'gonm' |  |
| Mathematical text layout | 'math' | This script tag is intended for use by math layout engines for features related to math layout—see [OpenType Layout tags used with the MATH Table](math#opentype-layout-tags-used-with-the-math-table) for more details. |
| Medefaidrin (Oberi Okaime, Oberi Ɔkaimɛ) | 'medf' |  |
| Meitei Mayek (Meithei, Meetei) | 'mtei' |  |
| Mende Kikakui | 'mend' |  |
| Meroitic Cursive | 'merc' |  |
| Meroitic Hieroglyphs | 'mero' |  |
| Miao | 'plrd' |  |
| Modi | 'modi' |  |
| Mongolian | 'mong' |  |
| Mro | 'mroo' |  |
| Multani | 'mult' |  |
| Musical Symbols | 'musc' |  |
| Myanmar | 'mymr' | Use of this tag is not recommended. Fonts supporting Unicode encoding of Myanmar script should use 'mym2' instead. |
| Myanmar v.2 | 'mym2' |  |
| Nabataean | 'nbat' |  |
| Nag Mundari | 'nagm' |  |
| Nandinagari | 'nand' |  |
| Newa | 'newa' |  |
| New Tai Lue | 'talu' |  |
| N'Ko | 'nko ' |  |
| Nüshu | 'nshu' |  |
| Nyiakeng Puachue Hmong | 'hmnp' |  |
| Odia | 'orya' |  |
| Odia v.2 | 'ory2' |  |
| Ogham | 'ogam' |  |
| Ol Chiki | 'olck' |  |
| Ol Onal | 'onao' |  |
| Old Italic | 'ital' |  |
| Old Hungarian | 'hung' |  |
| Old North Arabian | 'narb' |  |
| Old Permic | 'perm' |  |
| Old Persian Cuneiform | 'xpeo' |  |
| Old Sogdian | 'sogo' |  |
| Old South Arabian | 'sarb' |  |
| Old Turkic, Orkhon Runic | 'orkh' |  |
| Old Uyghur | 'ougr' |  |
| Osage | 'osge' |  |
| Osmanya | 'osma' |  |
| Pahawh Hmong | 'hmng' |  |
| Palmyrene | 'palm' |  |
| Pau Cin Hau | 'pauc' |  |
| Phags-pa | 'phag' |  |
| Phoenician | 'phnx' |  |
| Proto-Cuneiform | 'pcun' |  |
| Psalter Pahlavi | 'phlp' |  |
| Rejang | 'rjng' |  |
| Runic | 'runr' |  |
| Samaritan | 'samr' |  |
| Saurashtra | 'saur' |  |
| Seal (“Small Seal”) | 'seal' |  |
| Sharada | 'shrd' |  |
| Shavian | 'shaw' |  |
| Siddham | 'sidd' |  |
| Sidetic | 'sidt' |  |
| Sign Writing | 'sgnw' |  |
| Sinhala | 'sinh' |  |
| Sogdian | 'sogd' |  |
| Sora Sompeng | 'sora' |  |
| Soyombo | 'soyo' |  |
| Sumero-Akkadian Cuneiform | 'xsux' |  |
| Sundanese | 'sund' |  |
| Sunuwar | 'sunu' |  |
| Syloti Nagri | 'sylo' |  |
| Syriac | 'syrc' |  |
| Tagalog | 'tglg' |  |
| Tagbanwa | 'tagb' |  |
| Tai Le | 'tale' |  |
| Tai Tham (Lanna) | 'lana' |  |
| Tai Viet | 'tavt' |  |
| Tai Yo | 'tayo' |  |
| Takri | 'takr' |  |
| Tamil | 'taml' |  |
| Tamil v.2 | 'tml2' |  |
| Tangsa | 'tnsa' |  |
| Tangut | 'tang' |  |
| Telugu | 'telu' |  |
| Telugu v.2 | 'tel2' |  |
| Thaana | 'thaa' |  |
| Thai | 'thai' |  |
| Tibetan | 'tibt' |  |
| Tifinagh | 'tfng' |  |
| Tirhuta | 'tirh' |  |
| Todhri | 'todr' |  |
| Tolong Siki | 'tols' |  |
| Toto | 'toto' |  |
| Tulu-Tigalari | 'tutg' |  |
| Ugaritic Cuneiform | 'ugar' |  |
| Vai | 'vai ' |  |
| Vithkuqi | 'vith' |  |
| Wancho | 'wcho' |  |
| Warang Citi | 'wara' |  |
| Yezidi | 'yezi' |  |
| Yi | 'yi ' | For this tag value, NO-BREAK SPACE characters are used in this table rather than SPACE to ensure that two space characters are displayed. The actual tag byte sequence is 0x79 0x69 0x20 0x20. |
| Zanabazar Square (Zanabazarin Dörböljin Useg, Xewtee Dörböljin Bicig, Horizontal Square Script) | 'zanb' |  |
