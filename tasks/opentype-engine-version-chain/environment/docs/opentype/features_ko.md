
# Registered features, k-o (OpenType 1.9.1) - Typography | Microsoft Learn

[a - e](features_ae) | [f - j](features_fj) | k - o | [p - t](features_pt) | [u - z](features_uz)

## Tag: 'kern'

*Friendly name:* Kerning

*Registered by:* Microsoft/Adobe

*Function:* In horizontal text layout, used to adjust the amount of space between specific glyph combinations to provide optically consistent spacing between glyphs.

Although a well-designed typeface has consistent inter-glyph spacing overall, some glyph combinations require adjustment for improved legibility. Besides standard adjustment in the horizontal direction, this feature can provide “cross-stream” kerning in the Y text direction, make adjustments to glyph placement independent of the advance adjustment, and can supply size-dependent kerning data via device tables. Note that lookups for this feature can apply to runs of more than two glyphs.

This feature is not intended for use in vertical text layout. See the ['vkrn'](features_uz#vkrn) feature for vertical kerning.

This feature should only be used for intended spacing adjustments that can be overridden by the user. For required spacing adjustments not under user control, as might be needed for certain scripts, the ['dist'](features_ae#dist) feature should be used.

*Example:* The o is shifted closer to the T in the combination “To.”

*Recommended implementation:* The font adjusts metrics for pairs of glyphs (GPOS lookup type 2 or 8, xPlacement, xAdvance, yPlacement and yAdvance). These can be stored as one or more tables matching left and right classes, or as individual pairs. Additional adjustments may be provided for longer sequences of glyphs (e.g., triplets, quadruplets, etc.) and ordered to take precedence over pair kern adjustments of particular combinations.

A font should not apply kerning adjustments to fixed-width glyphs unless other features can be applied that adjust glyph metrics to be proportional. In new fonts, kerning of glyphs with default fixed-width metrics that have been adjusted to have proportional widths should be implemented using the ['apkn'](features_ae#apkn) feature rather than this feature. See below for more details.

When using a GPOS type 2 lookup for this feature, it is recommended that no positioning adjustment be applied to the second glyph in a pair. (That is, that valueFormat2 should be set to 0.) As a result, the next glyph pair to be processed after the lookup has been applied to this pair will start at the second glyph. In this way, every glyph in a sequence can undergo positioning adjustment as the first glyph in a pair. Otherwise, if positioning of the second glyph is adjusted, then the next glyph pair to be processed will begin with the glyph following the second glyph. That will result in kerning adjustment happening only for every *other* pair of glyphs.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* In most horizontal text layout, this feature should be active by default—see below for important exceptions. Applications may allow users to add further manually specified adjustments to suit specific needs and tastes. This feature should be optional: when activated by default for a text run, it should be possible for users to disable it for that run.

An exception to default activation can apply for CJK text. If a text run is formatted with a font that implements the ['palt'](features_pt#palt) feature, this feature should not be activated by default to glyphs covered by 'palt' lookups unless the 'palt' feature has been activated. (Glyphs covered by 'palt' lookups will have fixed-width metrics by default, and the kerning adjustments applied by this feature would be inappropriate for glyphs with fixed-width metrics.) An application can scan the coverage tables of 'palt' lookups to determine glyphs to be excluded from default activation. Alternately, an application can use the East Asian Width property, defined in [Unicode Standard Annex #11, East Asian Width](https://www.unicode.org/reports/tr11/), to determine characters likely to have fixed-width glyphs by default. Specifically, Unicode characters with East\_Asian\_Width property values *Wide* or *Fullwidth* may be assumed to have default fixed-width metrics in fonts that implement 'palt', and therefore excluded from default activation of the 'kern' feature.

In new fonts, however, glyphs that are fixed width by default but are made proportional by application of 'palt' should have their kerning implemented using the ['apkn'](features_ae#apkn) feature rather than the 'kern' feature. If a font implements both the 'palt' and 'apkn' features, then the 'kern' feature can be applied by default to all horizontal text. But if a font implements 'palt' but not 'apkn', then one of the techniques described in the previous paragraph should be used to condition default application of 'kern'.

If a font implements 'kern' but does not implement 'palt', then 'kern' should be applied by default to all horizontal text using that font. See below for other important feature interactions.

*Script/language sensitivity:* None.

*Feature interaction:* The 'kern' feature should not be used in combination with features that result in fixed-width metrics (['chws'](features_ae#chws), ['fwid'](features_fj#fwid), ['halt'](features_fj#halt), ['hwid'](features_fj#hwid), ['qwid'](features_pt#qwid), ['twid'](features_pt#twid), ['tnum'](features_pt#tnum)). If any of those features are activated on a text run, this feature should be disabled for that run.

This feature may be used in combination with the ['pkna'](features_pt#pkna) and ['pwid'](features_pt#pwid) features, which substitute alternate proportional-width glyphs for fixed-width glyphs. When those features are activated on a text run, this feature may be activated for that run, either by default or by user preference.

This feature may also be used in combination with the ['palt'](features_pt#palt) feature, which adjusts glyph metrics to be proportional. See details above regarding interaction between this feature and 'palt'. Also see recommendations above regarding use of the ['apkn'](features_ae#apkn) feature in fonts.

If kerning is activated by default as a result of activation of any of the above features that result in proportional widths, users should still have the option to disable this while the proportional-width feature is still active.

## Tag: 'lfbd'

*Friendly name:* Left Bounds

*Registered by:* Adobe

*Function:* Aligns glyphs by their apparent left extents at the left ends of horizontal lines of text, replacing the default behavior of aligning glyphs by their origins.

*Example:* Succeeding lines beginning with T, D and W would shift to the left by varying amounts when the text is left-justified and this feature is applied.

*Recommended implementation:* Values for affected glyphs describe the amount by which the placement and advance width should be altered (GPOS lookup type 1).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. In recommended usage, the application applies this feature to characters at the left end of a horizontal line when paragraphs are left or left and right justified.

*UI suggestion:* This feature should be inactive by default. Applications may expose to users direct control of this feature and also the Right Bounds feature (['rtbd'](features_pt#rtbd)), or may automatically activate the feature based on other paragraph layout settings.

*Script/language sensitivity:* None.

*Feature interaction:* Should not be applied to glyphs which use fixed-width features (e.g. ['fwid'](features_fj#fwid), ['halt'](features_fj#halt), ['hwid'](features_fj#hwid), ['qwid'](features_pt#qwid) and ['twid'](features_pt#twid)) or vertical features (e.g. ['vert'](features_uz#vert), ['vrt2'](features_uz#vrt2), ['vpal'](features_uz#vpal), ['valt'](features_uz#valt) and ['vhal'](features_uz#vhal)).

## Tag: 'liga'

*Friendly name:* Standard Ligatures

*Registered by:* Microsoft/Adobe

*Function:* Replaces a sequence of glyphs with a single glyph which is preferred for typographic purposes. This feature covers the ligatures which the designer/manufacturer judges should be used in normal conditions.

*Example:* The glyph for ffl replaces the sequence of glyphs f f l.

*Recommended implementation:* A lookup table for the 'liga' feature maps sequences of glyphs to corresponding ligatures (GSUB lookup type 4). Ligatures with more components need to be stored ahead of those with fewer components in order to be found. The set of standard ligatures will vary by design and script.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* This feature serves a critical function in some contexts and should be active by default.

*Script/language sensitivity:* None.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override.

## Tag: 'ljmo'

*Friendly name:* Leading Jamo Forms

*Registered by:* Microsoft

*Function:* Substitutes the form for a Hangul leading consonant jamo used in a jamo cluster.

*Example:* In Hangul script, the jamo cluster is composed of three parts: leading consonant, vowel, and trailing consonant. When a leading jamo is found, an alternate leading jamo form is substituted.

*Recommended implementation:* The default glyph for a leading jamo is mapped into an alternate form required for conjoining in a syllable (GSUB lookup type 1, or a contextual substitution referencing a type 1 lookup).

*Application interface:* In recommended usage, this feature triggers substitutions required for correct layout of certain scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Hangul script, particularly when Unicode conjoining jamo characters are used.

*Feature interaction:* This feature overrides the results of all other features.

## Tag: 'lnum'

*Friendly name:* Lining Figures

*Registered by:* Adobe (Modified by Adobe, this is the newer description)

*Function:* This feature changes selected non-lining figures (digits) to lining figures.

*Example:* The user invokes this feature in order to get lining figures, which fit better with all-capital text. Various characters designed to be used with figures may also be covered by this feature. In cases where lining figures are the default form, this feature would undo previous substitutions.

*Recommended implementation:* Glyphs for figures (digits) or other characters used in numbers (grouping or decimal separators) are mapped to corresponding lining forms (GSUB lookup type 1). If the default figures are non-lining, they too are mapped to the corresponding lining form.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* This feature should be inactive by default. Users can switch between the lining and oldstyle sets by turning this feature on or off. Note that this feature is distinct from the figure width features (['pnum'](features_pt#pnum) and ['tnum'](features_pt#tnum)). When the user invokes this feature, the application could inquire whether a change in width is also desired.

*Script/language sensitivity:* None.

*Feature interaction:* This feature is mutually exclusive to the Oldstyle Figures feature ('onum').

## Tag: 'locl'

*Friendly name:* Localized Forms

*Registered by:* Tiro Typeworks/Adobe

*Function:* Many scripts used to write multiple languages over wide geographical areas have developed localized variant forms of specific letters, which are used by individual literary communities. For example, several letters in the Bulgarian and Serbian alphabets have forms distinct from their Russian counterparts and from each other. In some cases, the localized form differs only subtly from the script “norm”, in others the forms are radically distinct. This feature enables localized forms of glyphs to be substituted for default forms.

*Example:* The user applies this feature to text to enable localized Bulgarian forms of Cyrillic letters; alternatively, the feature might enable localized Russian forms in a Bulgarian manufactured font in which the Bulgarian forms are the default characters.

*Recommended implementation:* For a given Unicode value, the font contains glyphs for two or more locales. A lookup table for the 'locl' feature maps GIDs for default forms to GIDs for corresponding localized alternatives. These are one-to-one substitutions (GSUB lookup type 1). Different language systems can have different implementations for this feature, with lookups that provide appropriate substitutions for each language system.

*Application interface:* In recommended usage, this feature triggers substitutions or positioning adjustments to provide the most appropriate typography for the language of content. This feature should always be applied. Language tagging or similar metadata indicating the language of the text should be used to select the most appropriate language system table within the font’s GPOS or GSUB table.

*UI suggestion:* Control of the feature should not generally be exposed to the user directly. Control over language-specific typography should be provided by language settings or by providing the user ability to choose between the alternative language systems supported in the font’s GPOS or GSUB tables.

*Script/language sensitivity:* None.

*Feature interaction:* This feature can be used in combination with any other feature.

## Tag: 'ltra'

*Friendly name:* Left-to-right Alternates

*Registered by:* Adobe

*Function:* This feature applies glyphic variants (other than mirrored forms) appropriate for left-to-right text (for mirrored forms, see ['ltrm'](features_ko#ltrm)).

*Recommended implementation:* These are required to be glyph substitutions, and it is recommended that they be one-to-one (GSUB lookup type 1).

*Application interface:* See section “Left-to-right and right-to-left text” on the [Advanced Typographic Extensions](ttochap1#ltrrtl) page.

*UI suggestion:* Control of this feature should not generally be exposed to the user.

*Script/language sensitivity:* Left-to-right runs of text.

*Feature interaction:* This feature is to be applied simultaneously with other pre-shaping features such as ['ccmp'](features_ae#ccmp) and ['locl'](features_ko#locl).

## Tag: 'ltrm'

*Friendly name:* Left-to-right Mirrored Forms

*Registered by:* Adobe

*Function:* This feature applies mirrored forms appropriate for left-to-right text. (For left-to-right glyph alternates, see ['ltra'](features_ko#ltra)).

*Example:* The Old South Arabian script is a case of a strong right-to-left script that can have lines laid out left-to-right, in which case some glyphs would need to be mirrored with the 'ltrm' feature.

*Recommended implementation:* These are required to be glyph substitutions, and it is recommended that they be one-to-one (GSUB lookup type 1).

*Application interface:* See section “Left-to-right and right-to-left text” on the [Advanced Typographic Extensions](ttochap1#ltrrtl) page.

*UI suggestion:* Control of this feature should not generally be exposed to the user.

*Script/language sensitivity:* Left-to-right runs of text; also see Example above.

*Feature interaction:* This feature is to be applied simultaneously with other pre-shaping features such as ['ccmp'](features_ae#ccmp) and ['locl'](features_ko#locl).

## Tag: 'mark'

*Friendly name:* Mark Positioning

*Registered by:* Microsoft

*Function:* Positions mark glyphs with respect to base glyphs.

*Example:* In the Arabic script, positioning the Hamza above the Yeh.

*Recommended implementation:* This feature can be implemented as a mark-to-base attachment lookup (GPOS LookupType = 4) or a mark-to-ligature attachment lookup (GPOS LookupType = 5).

*Application interface:* In recommended usage, this feature triggers positioning of mark glyphs required for correct layout. It should always be applied.

*UI suggestion:* Control of this feature should not generally be exposed to the user.

*Script/language sensitivity:* None.

*Feature interaction:* None.

## Tag: 'med2'

*Friendly name:* Medial Forms #2

*Registered by:* Microsoft

*Function:* Used to replace Alaph glyphs in the middle of Syriac words when the preceding base character can be joined to. This feature is used only for the Syriac script Alaph character.

*Example:* When an Alaph is preceded by a Heth, the Alaph would be replaced by an appropriate form.

*Recommended implementation:* Default alphabetic forms are mapped to corresponding medial forms (GSUB lookup type 1).

*Application interface:* In recommended usage, this feature triggers substitutions required for correct layout of Syriac text. The application should always apply the feature to the Alaph glyph when the preceding base character is a joining character.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used only with the Syriac script.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. See also ['init'](features_fj#init) and ['fina'](features_fj#fina).

## Tag: 'medi'

*Note:* This feature description was significantly revised in 2016.

*Friendly name:* Medial Forms

*Registered by:* Microsoft/Adobe

*Function:* Replaces glyphs for characters that have applicable joining properties with an alternate form when occurring in a medial context. This applies to characters that have the Unicode Joining\_Type property value Dual\_Joining.

Unicode Joining\_Type property values are obtained from the [Unicode Character Database](http://www.unicode.org/reports/tr44/tr44-18.html) (UCD). Specifically, Joining\_Type property values are documented in the UCD file, ArabicShaping.txt, the current version of which is available at http://www.unicode.org/Public/UCD/latest/ucd/ArabicShaping.txt.

*Example:* In an Arabic-script font, the application would apply the 'medi' feature to the letter ARABIC LETTER QAF (U+0642 “ق”) when it follows a left-joining character and precedes a right-joining character, thereby replacing the default “ق” glyph with its dual-joining, medial form.

*Recommended implementation:* The 'medi' feature is used to map default forms to corresponding dual-joining, medial forms. This will usually be implemented using a single substitution (type 1) GSUB lookup, though contextual substitution GSUB lookups (types 5, 6 or 8) could also be appropriate.

*Application interface:* In recommended usage, this feature triggers substitutions that are required for correct display of cursively-connecting scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements. The application is responsible for parsing character strings and identifying which of the joining-related features — initial forms ('init'), medial forms ('medi'), terminal forms ('fina'), and isolated forms ('isol') — to apply to which glyphs, based on character Joining\_Type properties. Additional factors, such as the presence of control characters, should also be considered.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Can be used in any script with joining behavior — that is, the scripts for which Joining\_Type properties are given explicitly in ArabicShaping.txt.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. See also ['fina'](features_fj#fina), ['init'](features_fj#init) and ['isol'](features_fj#isol).

## Tag: 'mgrk'

*Friendly name:* Mathematical Greek

*Registered by:* Adobe

*Function:* Replaces standard typographic forms of Greek glyphs with corresponding forms commonly used in mathematical notation (which are a subset of the Greek alphabet).

*Example:* The user applies this feature to U+03A3 (Sigma), and gets the glyph for U+2211 (summation).

*Recommended implementation:* A lookup table for the 'mgrk' feature maps Greek glyphs to the corresponding forms used for mathematics (GSUB lookup type 1).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. Note that the effect of this feature is effectively equivalent to a change of character codes. Besides the original character codes, the application should store the character codes for the new glyphs.

*UI suggestion:* This feature should be off by default in most applications. Math-oriented applications may activate this feature by default.

*Script/language sensitivity:* Used with Greek characters.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override.

## Tag: 'mkmk'

*Friendly name:* Mark to Mark Positioning

*Registered by:* Microsoft

*Function:* Positions marks with respect to other marks. Required in various non-Latin scripts like Arabic.

*Example:* In Arabic, the ligaturised mark Ha with Hamza above it can also be obtained by positioning these marks relative to one another.

*Recommended implementation:* This feature can be implemented as a mark-to-mark Attachment lookup (GPOS lookup type 6).

*Application interface:* In recommended usage, this feature triggers positioning of mark glyphs required for correct layout. It should always be applied.

*UI suggestion:* Control of this feature should not generally be exposed to the user.

*Script/language sensitivity:* None.

*Feature interaction:* None.

## Tag: 'mset'

*Friendly name:* Mark Positioning via Substitution

*Registered by:* Microsoft

*Function:* Positions Arabic combining marks in fonts for Windows 95 using glyph substitution.

This feature is not recommended for use in new fonts.

*Example:* In Arabic, the Hamza is positioned differently when placed above a Yeh Barree as compared to the Alef.

*Recommended implementation:* In contrast to the 'mark' feature, 'mset' uses glyph substitution to position marks on base glyphs. It replaces a default mark glyph with a correctly positioned mark glyph. The font designer specifies the position of the mark when describing the mark’s contour in the font file.

Microsoft’s Arabic fonts for Windows 95 used a contextual substitution lookup (GSUB lookup type 5) to implement this feature.

*Application interface:* When using legacy fonts that implement this feature, the feature triggers substitutions that are required for correct display and should be applied to the entire text run.

*UI suggestion:* Control of this feature should not generally be exposed to the user.

*Script/language sensitivity:* Used only in legacy Arabic fonts.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override.

## Tag: 'nalt'

*Friendly name:* Alternate Annotation Forms

*Registered by:* Adobe

*Function:* Replaces default glyphs with various notational forms (e.g., glyphs placed in open or solid circles, squares, parentheses, diamonds, or rounded boxes). In some cases, an annotation form could already be present, but the user might want a different one.

*Example:* The user invokes this feature to get the glyph of U+3200 (the circled form of “ga”) from U+3131 (hangul “ga”).

*Recommended implementation:* A lookup table for the 'nalt' feature maps GIDs for various standard forms to one or more corresponding annotation forms. While many of these substitutions are one-to-one (GSUB lookup type 1), others require a selection from a set (GSUB lookup type 3). A font developer can choose to build two tables (one for each lookup type) or only one which uses lookup type 3 for all substitutions. If more than one form is present, the set of forms for each character should be ordered consistently, both within the font and across the family.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. If implemented in a font using an alternate substitution lookup, the application selects one of the alternative glyphs based on user choice or other criteria.

*UI suggestion:* This feature should be inactive by default. When implemented in the font using an alternate substitution lookup, the application should provide a means for the user to select the desired form from the alternative glyphs mapped in the lookup. It can note the position of the selected form in a set of alternates and offer the glyph at that position as the default selection the next time this feature is invoked. In the absence of such prior information, the application may assume that the first glyph in a set is the preferred form, so the font developer should order them accordingly.

*Script/language sensitivity:* Used mostly in CJKV fonts, but can also be used for European scripts.

*Feature interaction:* This feature is mutually exclusive to most other features, which should be turned off when it’s applied. Exceptions include the ['vert'](features_uz#vert) and ['vrt2'](features_uz#vrt2) features, which may be used in addition.

## Tag: 'nlck'

*Friendly name:* NLC Kanji Forms

*Registered by:* Adobe Systems Inc.

*Function:* The National Language Council (NLC) of Japan has defined new glyph shapes for a number of JIS characters in 2000. The ['nlck'](features_ko#nlck) feature is used to access those glyphs.

*Example:* The glyph ![Default glyph for a Kanji character](images/nlc1.gif) is replaced by the glyph ![Substituted NLC glyph for the Kanji character](images/nlc2.gif).

*Recommended implementation:* One-for-one substitution of non-NLC glyphs by the corresponding NLC glyph (GSUB lookup type 1).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* This feature should be off by default.

*Script/language sensitivity:* Used only for Kanji characters in Japanese.

*Feature interaction:* This feature is mutually exclusive to the 'jp78', 'jp83', 'jp90' and similar features. It can be combined with the 'palt', 'vpal', 'vert' and 'vrt2' features.

## Tag: 'nukt'

*Friendly name:* Nukta Forms

*Registered by:* Microsoft

*Function:* Produces nukta forms in Indic scripts.

*Example:* In Hindi (Devanagari script), a consonant when combined with a nukta gives its nukta form.

*Recommended implementation:* A lookup table for the 'nukt' feature maps the sequence of a consonant followed by a nukta to the consonant’s nukta form (GSUB lookup type 4).

*Application interface:* In recommended usage, this feature triggers substitutions that are required for correct display of the given script. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Indic or other Brahmi-derived scripts.

*Feature interaction:* This feature is used in conjunction with certain other features to derive required forms of Indic scripts. The application is expected to process this feature and certain other features in an appropriate order to obtain the correct set of basic forms: ['nukt'](features_ko#nukt), ['akhn'](features_ae#akhn), ['rphf'](features_pt#rphf), ['rkrf'](features_pt#rkrf), ['pref'](features_pt#pref), ['blwf'](features_ae#blwf), ['half'](features_fj#half), ['pstf'](features_pt#pstf), ['cjct'](features_ae#cjct). Other discretionary features for optional typographic effects may also be applied. Lookups for such discretionary features should be processed after lookups for this feature have been processed.

## Tag: 'numr'

*Friendly name:* Numerators

*Registered by:* Adobe

*Function:* Replaces selected figures (digits) which precede a slash with numerator figures, and replaces the typographic slash with the fraction slash.

*Example:* When the fraction feature (['frac'](features_fj#frac)) is applied to the string 11/17, the application applies this feature to the initial portion of the string to turn the 11 into numerators, and the slash into a fraction slash.

*Recommended implementation:* Glyphs for figures (digits) or other characters used in numbers (grouping or decimal separators) are mapped to corresponding numerator glyphs; and the glyph for slash (U+002F) is mapped to a fraction slash (glyph for U+2044). Substitutions are one-to-one (GSUB lookup type 1).

*Application interface:* In recommended usage, this feature is applied automatically as part of the application’s implementation for the 'frac' feature. See the ['frac'](features_fj#frac) for details.

*UI suggestion:* In recommended usage, this feature is applied to sequences automatically by applications when the 'frac' feature is used, and direct user control is not required.

*Script/language sensitivity:* None.

*Feature interaction:* This feature supports application implementation for the ['frac'](features_fj#frac) feature. It may be used in combination with other substitution (GSUB) features, whose results it may override.

## Tag: 'onum'

*Friendly name:* Oldstyle Figures

*Registered by:* Microsoft/Adobe

*Function:* This feature changes selected figures from the default or lining style to oldstyle form.

*Example:* The user invokes this feature to get oldstyle figures, which fit better into the flow of normal upper- and lowercase text. Various characters designed to be used with figures may also have oldstyle versions.

*Recommended implementation:* Glyphs for figures (digits) or other characters used in numbers (grouping or decimal separators) are mapped to corresponding oldstyle forms (GSUB lookup type 1). If the default figures are non-lining, they too are mapped to the corresponding oldstyle form.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* This feature should be inactive by default. Users can switch between the default and oldstyle figure sets by turning this feature on or off. This feature is separate from the figure-width features ['pnum'](features_pt#pnum) and ['tnum'](features_pt#tnum). When the user changes figure style, the application could query whether a change in width is also desired.

*Script/language sensitivity:* None.

*Feature interaction:* This feature overrides the results of the Lining Figures feature ('lnum').

## Tag: 'opbd' (DEPRECATED)

*Friendly name:* Optical Bounds

*Registered by:* Adobe

*Function:* Aligns glyphs by their apparent left or right extents in horizontal setting, or apparent top or bottom extents in vertical setting, replacing the default behavior of aligning glyphs by their origins. Another name for this behavior would be visual justification. The optical edge of a given glyph is only indirectly related to its advance width or bounding box; this feature provides a means for getting true visual alignment.

*Example:* Succeeding lines beginning with T, D and W would shift to the left by varying amounts when the text is left-justified and this feature is applied. Succeeding lines ending with r, h and y would likewise shift to the right by differing degrees when the text is right-justified and this feature is applied.

*Recommended implementation:* Positioning adjustments (GPOS lookup type 1) are implemented in separate lookups associated with features for left, right, top and bottom line positions.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. The application implementation for this feature should make use of the 'lfbd' and ['rtbd'](features_pt#rtbd) features: the 'lfbd' feature should be applied automatically to glyphs at the left end of a horizontal line; the 'rtbd' feature should be applied automatically to glyphs at the right end of a horizontal line. It affects the line length, which should be taken into consideration by line-breaking and justification algorithms.

*UI suggestion:* This feature should be active by default.

*Script/language sensitivity:* None.

*Feature interaction:* Should not be applied to glyphs which use fixed-width features (e.g. ['fwid'](features_fj#fwid), ['halt'](features_fj#halt), ['hwid'](features_fj#hwid), ['qwid'](features_pt#qwid) and ['twid'](features_pt#twid)) or vertical features (e.g. ['vert'](features_uz#vert), ['vrt2'](features_uz#vrt2), ['vpal'](features_uz#vpal), ['valt'](features_uz#valt) and ['vhal'](features_uz#vhal)). Uses 'lfbd' and ['rtbd'](features_pt#rtbd) features.

## Tag: 'ordn'

*Friendly name:* Ordinals

*Registered by:* Adobe

*Function:* Replaces default alphabetic glyphs with the corresponding ordinal forms for use after figures. One exception to the follows-a-figure rule is the numero character (U+2116), which is actually a ligature substitution, but is best accessed through this feature.

*Example:* The user applies this feature to turn 2.o into 2.^o^ (abbreviation for secundo).

*Recommended implementation:* Glyphs for various lowercase letters are mapped to corresponding ordinal forms using a chained-context substitution (GSUB lookup type 6); and the sequence of default glyphs for “No” are mapped to a numero ligature glyph (GSUB lookup type 4).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. When processing lookups, context before or after the glyph sequence to which the feature is applied should be considered. Note that the effect of this feature could be effectively equivalent to a change of character codes. Besides the original character codes, the application should store the character codes for the new glyph.

*UI suggestion:* This feature should be off by default.

*Script/language sensitivity:* Used mostly for Latin script.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override.

## Tag: 'ornm'

*Friendly name:* Ornaments

*Registered by:* Adobe

*Function:* This is a dual-function feature, which uses two input methods to give the user access to ornament glyphs (e.g. fleurons, dingbats and border elements) in the font. One method replaces the bullet character with a selection from the full set of available ornaments; the other replaces specific “lower ASCII” characters with ornaments assigned to them. The first approach supports the general or browsing user; the second supports the power user.

*Example:* The user inputs “qwwwwwwwwwe” to form the top of a flourished box in Adobe Caslon, or inputs the bullet character, then chooses the thistle dingbat.

*Recommended implementation:* All ornament glyphs are mapped from the default glyph of the bullet character (U+2022) as alternates (GSUB lookup type 3); or ornament glyphs are mapped from the default glyph of corresponding alphanumeric characters (GSUB lookup type 1). A font developer can choose to build two tables (one for each lookup type) or only one which uses lookup type 3 for all substitutions. As in any alternate substitution lookup, alternates present in more than one face should be ordered consistently across a family, so that those alternates can work correctly when switching between family members.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. When this feature is invoked, the application must note whether the selected text is the bullet character (U+2022) or alphanumeric characters. In the first case, it retrieves the glyphs mapped from the bullet glyph, and gives the user a means to select from among them. In the second case, a substitution is applied directly.

*UI suggestion:* This feature should be inactive by default. When implemented in the font using an alternate substitution lookup (the bullet case), an application could display the forms sequentially in context, or present a palette showing all the forms at once, or give the user a choice between these approaches. Once the user has selected a specific ornament, that one should be the default selection the next time the bullet is typed. In the absence of such prior information, the application may assume that the first ornament in a set is the preferred form, so the font developer should order them accordingly.

*Script/language sensitivity:* None.

*Feature interaction:* This feature is mutually exclusive to all other substitution (GSUB) features, which should be turned off when it’s applied.
