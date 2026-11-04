
# Registered features, a-e (OpenType 1.9.1) - Typography | Microsoft Learn

a - e | [f - j](features_fj) | [k - o](features_ko) | [p - t](features_pt) | [u - z](features_uz)

## Tag: 'aalt'

*Friendly name:* Access All Alternates

*Registered by:* Adobe

*Function:* This feature makes all variations of a selected character accessible. This serves several purposes: An application might not support the feature by which the desired glyph would normally be accessed; the user could need a glyph outside the context supported by the normal substitution, or the user might not know what feature produces the desired glyph.

*Example:* A user inputs the P in Poetica and is presented with a choice of the four standard capital forms, the eight swash capital forms, the initial capital form and the small capital form.

*Recommended implementation:* A GSUB type 3 lookup subtable should normally be used. The type 3 formats arrange glyphs into groupings of related glyphs. For example, the default glyph for a Unicode character and the associated set of alternate glyphs for that character. The default glyph can be included in the set of alternates, in which case it should be the first alternate.

A GSUB type 1 lookup subtable could also be used if a default glyph has only one alternate. A lookup could combine type 3 and type 1 subtables. Other lookup subtable types are not recommended.

Ligature glyphs should not be included as alternates of the glyph for any ligature component. For example, an fi ligature glyph should not be included as an alternate for f. Ligature glyphs can be included in an alternate set if they are variants of another ligature glyph, however.

If different fonts within a typographic family have the same alternates, the ordering of glyphs within the alternate sets should match across these fonts so that corresponding alternates will be selected when switching between fonts in the family.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. The application determines the glyph ID for the default form of a given character (default glyph from the 'cmap' table with no features applied). It then checks to see whether the glyph ID is found in the coverage table of an 'aalt' lookup. If so, the application retrieves the alternate glyphs mapped in the alternate substitution lookup and selects one.

*UI suggestion:* The application should indicate to the user which glyphs in the user’s document have alternative forms (i.e., which are in the coverage table for 'aalt'), and provide some means for the user to select an alternate glyph. An application could display the forms sequentially in context, or present a palette showing all the forms at once, or give the user a choice between these approaches. The application may assume that the first glyph in a set is the preferred form, so the font developer should order them accordingly. When only one alternate exists, this feature could toggle directly between the alternate and default forms.

*Script/language sensitivity:* None.

*Feature interaction:* This feature may be used in combination with other features.

## Tag: 'abvf'

*Friendly name:* Above-base Forms

*Registered by:* Microsoft

*Function:* Substitutes the above-base form of a vowel. In particular, this feature is used for certain “split” vowels in Khmer or other Brahmi-derived scripts if the character has separate components that appear before and above a consonant, but the Unicode character for the vowel does not have a decomposition into separate characters for the two components.

*Example:* In Khmer script, U+17BE KHMER VOWEL SIGN OE needs to be split into a pre-base form and an above-base form. The above-base form of OE would be substituted to form the correct piece of the vowel that is displayed above the base consonant.

*Recommended implementation:* This feature is used to map the default glyph for OE to a glyph for the above-base component (GSUB lookup type 1).

*Application interface:* In recommended usage, this feature triggers substitutions required for correct display of Khmer script. It should be applied in the appropriate contexts, as determined by script-specific processing requirements. In particular, in a cluster in which the applicable split vowel is used, the application must insert the pre-base glyph into the correct location and then apply the above-base form feature to the split vowel.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Khmer script. May also be used for other Brahmi-derived scripts.

*Feature interaction:* This feature overrides the results of all other features.

## Tag: 'abvm'

*Friendly name:* Above-base Mark Positioning

*Registered by:* Microsoft

*Function:* Positions marks above base glyphs.

*Example:* In complex scripts like Devanagari (Indic), the Anuswar needs to be positioned above the base glyph. This base glyph can be a base consonant or conjunct. The base glyph and the presence/absence of other marks above the base glyph decides the location of the Anuswar, so that they do not overlap one another.

*Recommended implementation:* A lookup table for the 'abvm' feature provides positioning information (x,y) to enable mark positioning (GPOS lookup type 4, 5).

*Application interface:* In recommended usage, this feature triggers positioning of mark glyphs required for correct display of certain scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Indic or other Brahmi-derived scripts.

*Feature interaction:* Can be used to position default marks; or those that have been selected from a number of alternates based on contextual requirement using a feature like 'abvs'.

## Tag: 'abvs'

*Friendly name:* Above-base Substitutions

*Registered by:* Microsoft

*Function:* Substitutes a ligature for a base glyph and mark that’s above it.

*Example:* In complex scripts like Kannada (Indic), the vowel sign for the vowel I which a mark, is positioned above base consonants. This mark combines with the consonant Ga to form a ligature.

*Recommended implementation:* The 'abvs' feature is used to map consonant vowel sequences to corresponding ligature glyphs (GSUB lookup type 4).

*Application interface:* In recommended usage, this feature triggers substitutions required for correct display of certain scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Indic or other Brahmi-derived scripts.

*Feature interaction:* None.

## Tag: 'afrc'

*Friendly name:* Alternative Fractions

*Registered by:* Microsoft

*Function:* Replaces figures separated by a slash with an alternative form.

*Example:* The user enters 3/4 in a recipe and get the three-quarters nut fraction.

*Recommended implementation:* Sequences of glyphs for figures (digits) separated by a slash (U+002F) or fraction slash (U+2044) are mapped to a corresponding ligature glyph for the fraction (GSUB lookup type 4).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. The application applies the feature to a complete sequence of figures separated by slash (U+002F) or fraction slash (U+2044).

*UI suggestion:* This feature should be off by default.

*Script/language sensitivity:* None.

*Feature interaction:* This feature overrides the results of all other features.

## Tag: 'akhn'

*Friendly name:* Akhand

*Registered by:* Microsoft

*Function:* Preferentially substitutes default glyphs for a sequence of characters with a ligature. This substitution is done irrespective of any characters that may precede or follow the sequence.

*Example:* In Devanagari script, the form Kssa is considered an Akhand character (meaning unbreakable), and the sequence Ka, Halant, Ssa should always produce the ligature Kssa, irrespective of characters that precede/follow the above given sequence.

*Recommended implementation:* This feature is used to map sequences that form Akhands to the corresponding ligature glyph (GSUB lookup type 4).

*Application interface:* In recommended usage, this feature triggers substitutions required for correct display of certain scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Indic or other Brahmi-derived scripts.

*Feature interaction:* This feature is used in conjunction with certain other features to derive required forms of Indic scripts. The application is expected to process this feature and certain other features in an appropriate order to obtain the correct set of basic forms: ['nukt'](features_ko#nukt), ['akhn'](features_ae#akhn), ['rphf'](features_pt#rphf), ['rkrf'](features_pt#rkrf), ['pref'](features_pt#pref), ['blwf'](features_ae#blwf), ['half'](features_fj#half), ['pstf'](features_pt#pstf), ['cjct'](features_ae#cjct). Other discretionary features for optional typographic effects may also be applied. Lookups for such discretionary features should be processed after lookups for this feature have been processed.

## Tag: 'apkn'

*Friendly name:* Kerning for Alternate Proportional Widths.

*Registerd by:* Adobe/Microsoft

*Function:* Applies kerning adjustments to glyphs that have fixed advance widths by default but have been adjusted to proportional widths by application of the 'palt' feature.

*Example:* A user activates proportional metrics ('palt') in Japanese text, which results in some glyphs that had fixed advance widths by default now having proportional widths. By activating the 'apkn' feature as well, those glyphs are then kerned with other glyphs. For instance, the glyphs for U+30A2 “ア” and U+30CE “ノ” have full-width advances by default; the 'palt' feature adjusts these glyphs to have a narrower, proportional width; the 'apkn' feature then shifts these glyphs closer together in the combination “アノ”.

*Recommended implementation:* The font kerns Kanji, Kana, Latin, punctuation, or other glyphs that have fixed-width metrics by default if those glyphs are also adjusted to proportional widths by the 'palt' feature.

Primarily intended for use in the GPOS table. Positioning adjustments can be implemented using GPOS pair-adjustment (type 2) lookups. Single-adjustment and pair-adjustment lookups have simple additive effect, therefore relative ordering of lookups for this feature and type 1 or type 2 lookups for 'palt' or other similar features implemented using GPOS type 1 or type 2 lookups is not crucial.

For glyphs that have fixed widths by default but that can have metrics adjusted by the 'palt' feature, it is strongly recommended that those glyphs not be kerned with other glyphs using the 'kern' feature; these should only be kerned by application of this feature.

*UI suggestion:* This feature should be off by default. If the font used for a text run implements the 'palt' feature, activation on that text run should be possible only if the 'palt' feature has been activated on that run. If 'palt' has been activated for a run, then this feature may be automatically activated, but the UI should also provide a way for a user optionally to deactivate the feature while 'palt' is still activated.

*Script/language sensitivity:* Intended primarily for CJK fonts but can be used in fonts for any script that have fixed advance widths by default.

*Feature interaction:* Should only be implemented in fonts that also implement the 'palt' feature, which this feature is intended to complement. This feature should only be activated on a text run if 'palt' has been activated, but activation when that feature has been applied is optional and should be under user control. If activated on a run and later the 'palt' feature is deactivated for that run, then this feature should also be deactivated, as the kerning adjustments applied by this feature would not be appropriate for glyphs with fixed-width metrics.

In the recommended implementation for this feature, it acts only on characters that have fixed-width glyphs by default but that acquire proportional widths by application of the 'palt' feature. As a result, it can be deactivated on any text run that does not have 'palt' activated without any need to scan lookup tables to determine which glyphs do or do not require deactivation.

## Tag: 'blwf'

*Friendly name:* Below-base Forms

*Registered by:* Microsoft

*Function:* Substitutes the below-base form of a consonant in conjuncts.

*Example:* In complex scripts like Odia (Indic), the consonant Va has a below-base form that is used to generate conjuncts. Given a sequence Gha, Virama (Halant), Va; the below-base form of Va would be substituted to form the conjunct GhVa.

*Recommended implementation:* This feature is used to map a virama (halant) plus consonant sequence to the glyph for the below base form of the consonant (GSUB lookup type 4).

*Application interface:* In recommended usage, this feature triggers substitutions required for correct display of certain scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for many Indic or other Brahmi-derived scripts.

*Feature interaction:* This feature is used in conjunction with certain other features to derive required forms of Indic and Indic-related scripts. For Indic scripts, the application is expected to process this feature and certain other features in an appropriate order to obtain the correct set of basic forms: ['nukt'](features_ko#nukt), ['akhn'](features_ae#akhn), ['rphf'](features_pt#rphf), ['rkrf'](features_pt#rkrf), ['pref'](features_pt#pref), ['blwf'](features_ae#blwf), ['half'](features_fj#half), ['pstf'](features_pt#pstf), ['cjct'](features_ae#cjct). Other discretionary features for optional typographic effects may also be applied. Lookups for such discretionary features should be processed after lookups for this feature have been processed.

## Tag: 'blwm'

*Friendly name:* Below-base Mark Positioning

*Registered by:* Microsoft

*Function:* Positions marks below base glyphs.

*Example:* In complex scripts like Gujarati (Indic), the vowel sign U needs to be positioned below base consonant/conjuncts that form the base glyph. This position can vary depending on the base glyph, as well as the presence/absence of other marks below the base glyph.

*Recommended implementation:* A lookup table for the 'blwm' feature provides positioning information (x,y) to enable mark positioning (GPOS lookup type 4, 5).

*Application interface:* In recommended usage, this feature triggers positioning of mark glyphs required for correct display of certain scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Indic or other Brahmi-derived scripts.

*Feature interaction:* Can be used to position default marks; or those that have been selected from a number of alternates based on contextual requirement using a feature like 'blws'.

## Tag: 'blws'

*Friendly name:* Below-base Substitutions

*Registered by:* Microsoft

*Function:* Produces ligatures that comprise of base glyph and below-base forms.

*Example:* In the Malayalam script (Indic), the conjunct Kla, requires a ligature which is formed using the base glyph Ka and the below-base form of consonant La. This feature can also be used to substitute ligatures formed using base glyphs and below base matras in Indic scripts.

*Recommended implementation:* The 'blws' feature is used to map consonant conjunct sequences or consonant vowel sequences to corresponding ligature glyphs (GSUB lookup type 4).

*Application interface:* In recommended usage, this feature triggers substitutions required for correct display of certain scripts. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Indic or other Brahmi-derived scripts.

*Feature interaction:* This feature overrides the results of all other features.

## Tag: 'calt'

*Friendly name:* Contextual Alternates

*Registered by:* Adobe

*Function:* In specified situations, replaces default glyphs with alternate forms which provide better joining behavior. Used in script typefaces which are designed to have some or all of their glyphs join.

*Example:* In Caflisch Script, o is replaced by o.alt2 when followed by an ascending letterform.

*Recommended implementation:* A lookup table for the 'calt' feature specifies the context in which each substitution occurs, and maps one or more default glyphs to replacement glyphs (GSUB lookup type 6).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* This feature should be active by default.

*Script/language sensitivity:* Can be used with scripts that have optional, cursive styles.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override.

## Tag: 'case'

*Friendly name:* Case-sensitive Forms

*Registered by:* Adobe

*Function:* Shifts various punctuation marks up to a position that works better with all-capital sequences or sets of lining figures; also changes oldstyle figures to lining figures. By default, glyphs in a text face are designed to work with lowercase characters. Some characters should be shifted vertically to fit the higher visual center of all-capital or lining text. Also, lining figures are the same height (or close to it) as capitals, and fit much better with all-capital text.

*Example:* The user selects a block of text and applies this feature. The dashes, bracketing characters, guillemet quotes and the like shift up to match the capitals, and oldstyle figures change to lining figures.

*Recommended implementation:* The font may implement this change by substituting different glyphs (GSUB lookup type 1) or by repositioning the original glyphs (GPOS lookup type 1, xPlacement and yPlacement).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* It would be good to apply this feature (or turn it off) automatically when the user changes case on a sequence of more than one character. Applications could also detect words consisting only of capitals, and apply this feature based on user preference settings.

*Script/language sensitivity:* Applies only to European scripts; particularly prominent in Spanish-language setting.

*Feature interaction:* This feature overrides the results of other features affecting the figures (e.g. ['onum'](features_ko#onum) and ['tnum'](features_pt#tnum)).

## Tag: 'ccmp'

*Friendly name:* Glyph Composition / Decomposition

*Registered by:* Microsoft

*Function:* To minimize the number of glyph alternates, it is sometimes desirable to decompose the default glyph for a character into two or more glyphs. Additionally, it may be preferable to compose default glyphs for two or more characters into a single glyph for better glyph processing. This feature permits such composition/decomposition. The feature should be processed as the first feature processed, and should be processed only when it is called.

*Example:* In Syriac, the character 0x0732 is a combining mark that has a dot above AND a dot below the base character. To avoid multiple glyph variants to fit all base glyphs, the default glyph for the character is decomposed into two glyphs: a dot above and a dot below. These two glyphs can then be correctly placed using GPOS. In Arabic it might be preferred to combine the shadda with fatha (0x0651, 0x064E) into a ligature before processing shapes. This allows the font vendor to do special handling of the mark combination when doing further processing without requiring larger contextual rules.

*Recommended implementation:* Glyphs for multiple characters are mapped to a single glyph (GSUB lookup type 4), or the glyph for a character is mapped to a sequence of glyphs (GSUB lookup type 2).

*Application interface:* This feature should always be applied.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* None.

*Feature interaction:* This feature needs to be implemented prior to any other feature.

## Tag: 'cfar'

*Friendly name:* Conjunct Form After Ro

*Registered by:* Microsoft

*Function:* Substitutes alternate below-base or post-base forms in Khmer script when occurring after conjoined Ro (“Coeng Ra”).

In Khmer script, the conjoined form of Ro is reordered to the left of the base consonant. It wraps under the base consonant, however, and so can interact typographically with below-base or post-base conjoined consonant and vowel forms. After the application has reordered the glyph for the conjoined Ro, it is no longer in the immediate context of glyphs for below-base or post-base forms. The application can detect this and apply this feature over the range for the below-base and post-base conjoining forms, triggering lookups to substitute alternate below-base or post-base forms as may be needed.

*Example:* In the Khmer script, Coeng Ro is denoted by a pre-base conjoining form, and Coeng Yo is denoted by a post-base conjoining form, but in both cases part of the form wraps under the base. The consonant cluster TRYo is denoted with an alternate form of Coeng Ya that descends lower so that it does not collide below the base with the Coeng Ro.

*Recommended implementation:* A GSUB type 1 lookup substitutes a below-base or post-base conjoining form by an alternate form.

*Application interface:* In recommended usage, this feature triggers substitutions that are required for correct display of Khmer script. It should be applied in the appropriate contexts, as determined by script-specific processing requirements. In particular, the application is expected to apply this feature if a syllable contains a Coeng Ra followed by other conjoining consonants or vowels.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Khmer script.

*Feature interaction:* This feature is used in conjunction with certain other features to derive required forms of Khmer script. Other discretionary features for optional typographic effects may also be applied. Lookups for such discretionary features should be processed after lookups for this feature have been processed.

## Tag: 'chws'

*Friendly name:* Contextual Half-width Spacing

*Registered by:* Adobe/W3C

*Function:* Contextually re-spaces glyphs that have full-em widths by default, fitting them onto half-em horizontal widths to approximate more sophisticated text layout, such as what is described in [Requirements for Japanese Text Layout (JLREQ)](https://www.w3.org/TR/jlreq/) or similar CJK text-layout specifications that expect half-width forms of characters whose default glyphs are full-width. This feature differs from ['halt'](features_fj#halt) in that the re-spacing is contextual. This feature may be invoked to get better fit for punctuation or symbol glyphs without disrupting the monospaced alignment, such as for UIs or terminal apps.

(In JLREQ, see, in particular, [3.1.4 Positioning of Consecutive Opening Brackets, Closing Brackets, Commas, Full Stops and Middle Dots](https://www.w3.org/TR/jlreq/?lang=en#positioning_of_consecutive_opening_brackets_closing_brackets_comma_full_stops_and_middle_dots), and [B. Spacing between Characters](https://www.w3.org/TR/jlreq/?lang=en#spacing_between_characters). See also [Requirements for Chinese Text Layout (CLREQ)](https://www.w3.org/TR/clreq/), [Requirements for Hangul Text Layout (KLREQ)](https://www.w3.org/TR/klreq/).)

*Example:* When FULLWIDTH RIGHT PARENTHESIS (U+FF09; “）”) is followed by IDEOGRAPHIC COMMA (U+3001; “、”), the former is re-spaced to remove half-em of width between them.

*Recommended implementation:* The font specifies positioning adjustments for pairs of glyphs (GPOS lookup type 2 or 8, xPlacement, xAdvance, yPlacement, and yAdvance). Pairs can be defined using left and right classes of glyphs or using specific glyph IDs. Additional adjustments may be provided for larger sequences of glyphs (e.g., triplets, quadruplets, etc.) and ordered to take precedence over pair kern adjustments of particular combinations.

When using a GPOS type 2 lookup for this feature, it is recommended that no positioning adjustment be applied to the second glyph in a pair. (That is, that valueFormat2 should be set to 0.) As a result, the next glyph pair to be processed after the lookup has been applied to this pair will start at the second glyph. In this way, every glyph in a sequence can undergo positioning adjustment as the first glyph in a pair. Otherwise, if positioning of the second glyph is adjusted, then the next glyph pair to be processed will begin with the glyph following the second glyph. That will result in spacing adjustment happening only for every *other* pair of glyphs.

*Application interface:* If a layout engine supports advanced layout for CJK text as described in [CLREQ](https://www.w3.org/TR/clreq/), [JLREQ](https://www.w3.org/TR/jlreq/) or [KLREQ](https://www.w3.org/TR/klreq/), this feature should not be used. Otherwise, this feature should always be supported for horizontal layout of CJK text.

*UI suggestion:* This feature should not be used in combination with a layout engine that independently provides advanced layout as described in [CLREQ](https://www.w3.org/TR/clreq/), [JLREQ](https://www.w3.org/TR/jlreq/) or [KLREQ](https://www.w3.org/TR/klreq/). For applications that provide such advanced layout, it may be appropriate not to expose control of this feature to users. In applications that do not support such advanced layout, this feature should be enabled by default for horizontal layout of CJK text. See below for details on important feature interactions.

*Script/language sensitivity:* Used mostly in CJKV fonts.

*Feature interaction:* This feature is mutually exclusive to all other horizontal glyph-width features (['fwid'](features_fj#fwid), ['halt'](features_fj#halt), ['hwid'](features_fj#hwid), ['palt'](features_pt#palt), ['pkna'](features_pt#pkna), ['pwid'](features_pt#pwid), ['qwid'](features_pt#qwid), ['twid'](features_pt#twid), ['pnum'](features_pt#pnum), ['tnum'](features_pt#tnum)), which should be turned off when this feature is applied. When applied to a text run, the 'apkn' and ['kern'](features_ko#kern) features should be disabled for that run. See also ['vchw'](features_uz#vchw).

## Tag: 'cjct'

*Friendly name:* Conjunct Forms

*Registered by:* Microsoft

*Function:* Produces conjunct forms of consonants in Indic scripts. This is similar to the Akhands feature, but is applied at a different sequential point in the process of shaping an Indic syllable.

Indic scripts are associated with conjoining-consonant behaviors, such as the use of “half” forms. Some consonants might not have half forms and not exhibit conjoining behavior when combined with certain consonants, yet may conjoin as ligature forms with other consonants. Whether a given pair of consonants conjoins may impact other shaping behaviors for a syllable, such as where a reordering vowel mark or reph is placed. The Conjunct Forms feature can be used at a point in the shaping process immediately before final reordering such that the application can determine whether a reordering vowel or reph is placed in relation to the consonants.

More generally, the Akhands feature and Conjunct Forms feature can be used at two points in the shaping of an Indic syllable, together with other features such as Half Forms and Below Forms applied in between, providing the font developer with flexibility in how the shapes for Indic syllables are derived from the default glyphs for the character sequence.

*Example:* In Hindi (Devanagari script), the consonant cluster DGa is denoted with a conjunct ligature form.

*Recommended implementation:* A glyph for a consonant conjunct form is mapped from a sequence of two or more glyphs for consonants separated by virama (halant) (e.g., C H C, or C H C H C, etc.); or from sequences of glyphs involving simpler consonant conjuncts or conjoining consonant forms resulting from earlier substitutions from default consonant + virama + consonant (+ etc.) sequences (GSUB lookup type 4).

*Application interface:* In recommended usage, this feature triggers substitutions that are required for correct display of the given script. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Devanagari and other Indic or Brahmi-derived scripts.

*Feature interaction:* This feature is used in conjunction with certain other features to derive required forms of Indic scripts. The application is expected to process this feature and certain other features in an appropriate order to obtain the correct set of basic forms: ['nukt'](features_ko#nukt), ['akhn'](features_ae#akhn), ['rphf'](features_pt#rphf), ['rkrf'](features_pt#rkrf), ['pref'](features_pt#pref), ['blwf'](features_ae#blwf), ['half'](features_fj#half), ['pstf'](features_pt#pstf), ['cjct'](features_ae#cjct). Other discretionary features for optional typographic effects may also be applied. Lookups for such discretionary features should be processed after lookups for this feature have been processed.

## Tag: 'clig'

*Friendly name:* Contextual Ligatures

*Registered by:* Adobe

*Function:* Replaces a sequence of glyphs with a single glyph which is preferred for typographic purposes. Unlike other ligature features, 'clig' specifies the context in which the ligature is recommended. This capability is important in some script designs and for swash ligatures.

*Example:* The glyph for ft replaces the sequence f t in Bickham Script, except when preceded by an ascending letter.

*Recommended implementation:* A lookup table for the 'clig' feature maps sequences of glyphs to corresponding ligatures in a chained context (GSUB lookup type 8). Ligatures with more components need to be stored ahead of those with fewer components in order to be found. The set of contextual ligatures will vary by design and script.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. When processing lookups, context before or after the glyph sequence to which the feature is applied should be considered.

*UI suggestion:* This feature should be active by default.

*Script/language sensitivity:* None.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. See also 'dlig'.

## Tag: 'cpct'

*Friendly name:* Centered CJK Punctuation

*Registered by:* Adobe

*Function:* Centers specific punctuation marks for those fonts that do not include centered and non-centered forms.

*Example:* The user may invoke this feature in a Chinese font to get centered punctuation in case it is desired. Examples include U+3001 and U+3002, including their vertical variants, specifically U+FE11 and U+FE12, respectively.

*Recommended implementation:* The font specifies X- and Y-axis adjustments for a small number of full-width glyphs (GPOS lookup type 1, xPlacement, xAdvance, yPlacement and yAdvance).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* This feature should be off by default.

*Script/language sensitivity:* Used primarily in Chinese fonts.

*Feature interaction:* This feature is mutually exclusive to all other glyph-width features (e.g. ['tnum'](features_pt#tnum), ['fwid'](features_fj#fwid), ['hwid'](features_fj#hwid), ['halt'](features_fj#halt), ['palt'](features_pt#palt), ['twid'](features_pt#twid)), which should be turned off when it is applied.

## Tag: 'cpsp'

*Friendly name:* Capital Spacing

*Registered by:* Adobe

*Function:* Globally adjusts inter-glyph spacing for all-capital text. Most typefaces contain capitals and lowercase characters, and the capitals are positioned to work with the lowercase. When capitals are used for words, they need more space between them for legibility and esthetics. This feature would not apply to monospaced designs.

*Example:* The user sets a title in all caps, and the Capital Spacing feature opens the spacing.

*Recommended implementation:* A lookup table for the 'cpsp' feature stores alternate advance widths for the capital letters covered, generally increasing them by a uniform percentage (GPOS lookup type 1, xPlacement and xAdvance).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. The application may rely on the user to apply this feature (e.g., by selecting text for a change to all-caps) or apply its own heuristics for recognizing words consisting of capitals.

*UI suggestion:* This feature should be on by default. Applications may want to allow the user to override the percentage of adjustment to fit individual tastes and functions.

*Script/language sensitivity:* Should not be used in connecting scripts (e.g. most Arabic).

*Feature interaction:* May be used in combination with any other feature. Note that positioning adjustment effects of this and other features, such as ['kern'](features_ko#kern), can be additive.

## Tag: 'cswh'

*Friendly name:* Contextual Swash

*Registered by:* Adobe

*Function:* This feature replaces default character glyphs with corresponding swash glyphs in a specified context. More than one swash alternate may be provided for a given character.

*Example:* The user sets the word “HOLIDAY” in Poetica with this feature active, and is presented with a choice of three alternate forms appropriate for an initial H and one alternate appropriate for a medial L.

*Recommended implementation:* A lookup table for the 'cswh' feature maps GIDs for default forms to those for one or more corresponding swash forms in a chained context (GSUB lookup type 8). If more than one contextual alternate is provided, the nested substitution lookup can be implemented using alternate substitution (GSUB lookup type 3). If several styles of swash are present across the font, the set of forms for each character should be ordered consistently.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. If the font is implemented using an alternate substitution lookup, the application should provide a means for the user to select the one desired.

*UI suggestion:* This feature should be inactive by default. When implemented in the font using an alternate substitution lookup (GSUB type 3), an application could display the forms sequentially in context, or present a palette showing all the forms at once, or give the user a choice between these approaches. The application may assume that the first glyph in a set is the preferred form, so the font developer should order them accordingly.

*Script/language sensitivity:* Not used for ideographic scripts.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. See also ['swsh'](features_pt#swsh) and ['init'](features_fj#init).

## Tag: 'curs'

*Friendly name:* Cursive Positioning

*Registered by:* Microsoft

*Function:* This feature is used to position adjacent glyphs for cursive connections.

*Example:* In Arabic, the Meem followed by a Reh are cursively positioned by overlapping the exit point of the Meem on the entry point of the Reh.

*Recommended implementation:* Lookup tables for the 'curs' feature provide entry and exit points (x,y) for glyphs to be cursively positioned (GPOS lookup type 3).

*Application interface:* In recommended usage, this feature triggers positioning adjustments that are required for correct display of fonts that implement cursive joining behavior. It should always be applied.

*UI suggestion:* Control of this feature should not normally be exposed to the user.

*Script/language sensitivity:* Used primarily for Arabic and other cursively connecting scripts, and also any other scripts for which cursive styles can optionally be used.

*Feature interaction:* None.

## Tag: 'cv01' – 'cv99'

*Friendly name:* Character Variant 1 – Character Variant 99

*Registered by:* Microsoft

*Function:* A font may have stylistic-variant glyphs for one or more characters where the variations for one character are not systematically related to those for other characters. Or, a variation may exist for a character and its casing pair or related pre-composed characters, but not be applicable to other unrelated characters. In some usage scenarios, it may be necessary to provide the application with control over glyph variations for different Unicode characters individually.

The function of these features is similar to the function of the Stylistic Alternates feature [('salt')](features_pt#salt) and the Stylistic Set features (see ['ss01' – 'ss20'](features_pt#ssxx)). Whereas the Stylistic Set features assume recurring stylistic variations that apply to a broad set of Unicode characters, however, these features are intended for scenarios in which particular characters have variations not applicable to a broad set of characters. The Stylistic Alternates feature provides access to glyph variants but does not allow an application to control these on a character-by-character basis; the Character Variant features provide the greater granularity of control.

The function of these features is also related to that of the Localized Forms [('locl')](features_ko#locl) feature, in that particular variations for a character may be preferred for particular languages. In practice, though, it might not be feasible to associate particular glyph variants with particular language systems for all the relevant languages; for example, the requirements of particular languages might not be known when a font is being developed.

The distinction between these features and the Stylistic set features is most easily understood in terms of variants applying to a single character versus variants applying across a range of characters. In practice, if a variant applies to a character in a bicameral script, then the casing-pair character might have the same variant. Also, Unicode includes pre-composed characters for certain base + mark combinations, hence a single abstract character could be incorporated into a number of Unicode characters. Therefore, a variant for a particular abstract character could be applicable to several related Unicode characters. The Character Variant features can be used for sets of related characters in these cases. The key distinction between such use and the intended use for Stylistic Set features is that a Character Variant feature should apply only to one character or a set of characters closely related in this way, while Stylistic Set features are intended for broader sets of characters.

*Recommended implementation:* A 'cv*XX*' lookup table maps the GID for the default form of a character to the GIDs for stylistic alternates of that character. Each 'cv*XX*' feature uses alternate substitutions (GSUB lookup type 3). (If there is only one variant for a character, a single-substitution lookup, type 1, can also be used.) A given 'cv*XX*' feature acts on a single glyph or on multiple glyphs for closely related characters that have corresponding variants. Within each 'cv*XX*' feature, the number of variants should be identical for all glyphs, and the ordering of glyphs in lookup arrays should correspond such that the n^th^ variants of all glyphs are corresponding variants.

The featureParamsOffset field of the Feature Table of these GSUB features may be set to 0, or to an offset to a Feature Parameters table. The Feature Parameters table for this feature is structured as follows:

| Type | Name | Description |
| --- | --- | --- |
| uint16 | format | Format number is set to 0. |
| uint16 | featUiLabelNameId | The 'name' table name ID that specifies a string (or strings, for multiple languages) for a user-interface label for this feature. (May be NULL.) |
| uint16 | featUiTooltipTextNameId | The 'name' table name ID that specifies a string (or strings, for multiple languages) that an application can use for tooltip text for this feature. (May be NULL.) |
| uint16 | sampleTextNameId | The 'name' table name ID that specifies sample text that illustrates the effect of this feature. (May be NULL.) |
| uint16 | numNamedParameters | Number of named parameters. (May be zero.) |
| uint16 | firstParamUiLabelNameId | The first 'name' table name ID used to specify strings for user-interface labels for the feature parameters. (Must be zero if numParameters is zero.) |
| uint16 | charCount | The count of characters for which this feature provides glyph variants. (May be zero.) |
| uint24 | character[charCount] | The Unicode Scalar Value of the characters for which this feature provides glyph variants. |

If a font supports two or more character variant features, use of Feature Parameters tables should be consistent across all of these features.

The name ID provided by featUiLabelNameId is intended to provide a user-interface string for the feature; for example, “Capital-eng variants”. If set to NULL, no 'name' table string is used for the feature name.

The name ID provided by featUiTooltipTextNameId is intended to provide a user-interface string that provides a brief description of the feature that applications can use in popup “tooltip” help windows (e.g. “Select glyph variants for capital eng.”). If set to NULL, no 'name' table string is used for the feature “tooltip” help text.

The name ID provided by sampleTextNameId is intended to provide a string that can be used in a user-interface to illustrate the effect of the feature. If multiple characters are affected by the feature or if the feature affects a combining mark, it might not be evident to an application what string to use to present an illustrative sample; a 'name' table string can be provided for that purpose.

If numNamedParameters is non-zero, then firstParamUiLabelNameId and numNamedParameters specify a sequence of consecutive name IDs in the 'name' table. These are used to provide user-interface strings for individual variants. The range of name IDs start at firstParamUiLabelNameId and end at firstParamUiLabelNameId + numNamedParameters - 1. Each of these name IDs corresponds to a feature parameter value used to select a particular GID from the array of GIDs returned by a type 3 substitution lookup; the relation between parameter values and name IDs is: name ID = parameter + firstParamUiLabelNameId - 1. The value of numNamedParameters should not exceed the number of alternate glyphs in lookups associated with the feature. However, the number of GIDs in the returned array for a GSUB type 3 lookup should not be assumed to be equal to numNamedParameters: numNamedParameters should not be more than the number of GIDs in the array, but it may be less. If numNamedParameters is zero, then no 'name' table strings are associated with feature parameters.

The values of featUiLabelNameId, featUiTooltipTextNameId and firstParamUiLabelNameId are expected to be in the font-specific name ID range (256–32767), though that is not a requirement in this Feature Parameters specification. The value of firstParamUiLabelNameId + numNamedParameters - 1 should not exceed 32767.

The user-interface label for the feature, for “tooltip” help text, or for feature parameters can be provided in multiple languages. English strings for each should be included as a fallback. A sample-text string likely would not need to be localized, though different sample-text strings for different UI languages can be used. If only one sample-text string is provided, applications may use it with any UI language.

The charCount field and character array are used to identify the Unicode characters for which this feature provides glyph variants. Applications can use this information in presenting user interface or for other purposes. Content of the character list is at the discretion of the font developer — the list may be exhaustive, representative, or empty — and does not affect the operation of the feature. If a font developer chooses not to include such information, charCount may be set to zero, in which case no character array can be included.

It is left to the discretion of application developers to determine whether or how to use the data provided in the feature parameters table or associated strings in the 'name' table.

Since the strings provided using this feature parameter table will be used in application user interface, length is an important consideration. Strings should be as short as possible. It is recommended that the length of the feature or feature-parameter names be 25 characters or less, and that the length of “tooltip” help text be 250 characters or less.

*Application interface:* Discretionary features: can be applied to glyph runs based on document markup, user control or other application criteria. The application is responsible for counting and enumerating the number of features in the font with tag names of the format 'cv01' to 'cv99', and for presenting the user with an appropriate selection mechanism. The application is also responsible for interpreting any feature parameter tables (if the application developer wishes to use that data) and presenting referenced strings in a user interface. If the font is implemented using an alternate substitution lookup, the application may use an index parameter as an index into the array of mapped glyph IDs.

*UI suggestion:* This feature should be off by default. An application can display glyph variants for a given character as a glyph palette in the user interface. If a Feature Parameters table is provided, the feature UI label or the named parameter UI labels (if provided) can be presented in the application user interface; or the sample-text string (if provided) can be presented in the application user interface.

*Script/language sensitivity:* None.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. Note that after a 'cv*XX*' feature has been applied, the user may wish to apply other typographic features, e.g. 'smcp'; font developers are responsible for ordering substitution lookups to obtain desired user experience. If it is to be used in conjunction with a complex script that requires obligatory substitution of ligatures or contextual forms, this feature should be applied before features for obligatory script behaviors.

## Tag: 'c2pc'

*Friendly name:* Petite Capitals From Capitals

*Registered by:* Tiro Typeworks / Emigre

*Function:* This feature turns capital characters into petite capitals. It is generally used for words which would otherwise be set in all caps, such as acronyms, but which are desired in petite-cap form to avoid disrupting the flow of text. See the ['pcap'](features_pt#pcap) feature description for notes on the relationship of caps, smallcaps and petite caps.

*Example:* The user types UNICEF or NASA, applies 'c2pc' and gets petite cap text.

*Recommended implementation:* A lookup table for the 'c2pc' feature maps capital glyphs to the corresponding petite cap forms (GSUB lookup type 1).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. Applications may use this feature in an implementation to support petite-cap formatting applied to text with language-specific case-mapping logic. See ['pcap'](features_pt#pcap) and ['smcp'](features_pt#smcp) for additional information.

*UI suggestion:* This feature should be off by default.

*Script/language sensitivity:* Used for bicameral scripts (i.e. those with case differences), such as Latin, Greek, Cyrillic, and Armenian.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. Also see ['pcap'](features_pt#pcap).

## Tag: 'c2sc'

*Friendly name:* Small Capitals From Capitals

*Registered by:* Adobe

*Function:* This feature turns capital characters into small capitals. It is generally used for words which would otherwise be set in all caps, such as acronyms, but which are desired in small-cap form to avoid disrupting the flow of text.

*Example:* The user types UNICEF or SCUBA, applies 'c2sc' and gets small cap text.

*Recommended implementation:* A lookup table for the 'c2sc' feature maps capital glyphs to the corresponding small-cap forms (GSUB lookup type 1).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. Applications may use this feature in an implementation to support small-cap formatting applied to text with language-specific case-mapping logic. See ['smcp'](features_pt#smcp) for additional information.

*UI suggestion:* This feature should be off by default.

*Script/language sensitivity:* Used for bicameral scripts (i.e. those with case differences), such as Latin, Greek, Cyrillic, and Armenian.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. Also see ['smcp'](features_pt#smcp).

## Tag: 'dist'

*Friendly name:* Distances

*Registered by:* Microsoft

*Function:* Provides a means to control distance between glyphs.

This feature should only be used for required positioning adjustments, not as an alternate kerning feature. For spacing adjustments that can be controlled by the user, the ['kern'](features_ko#kern) feature should be used.

*Example:* In Telugu script, adjust the placement of subjoined consonant forms to avoid collision with other glyphs.

*Recommended implementation:* The font provides distances by which a glyph needs to move towards or away from another glyph (GPOS lookup type 1, type 2, or contextual lookups that reference nested type 1 or type 2 lookups).

*Application interface:* In recommended usage, this feature triggers positioning adjustments that are required for correct display of the given script. It should be applied in the appropriate contexts, as determined by script-specific processing requirements.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for Indic or other Brahmi-derived scripts.

*Feature interaction:* This and other certain other features, such as ['kern'](features_ko#kern), are typically implemented using single or pair adjustment positioning lookups. Note that positioning adjustment effects of this and other features can be additive. The glyphs affected by the 'dist' and 'kern' features should be distinct unless it is intended that their effects can be combined.

## Tag: 'dlig'

*Friendly name:* Discretionary Ligatures

*Registered by:* Adobe

*Function:* Replaces a sequence of glyphs with a single glyph which is preferred for typographic purposes. This feature covers those ligatures which may be used for special effect, at the user’s preference.

*Example:* The glyph for ct replaces the sequence of glyphs c t, or U+322E (Kanji ligature for “Friday”) replaces the sequence U+91D1 U+66DC U+65E5.

*Recommended implementation:* A lookup table for the 'dlig' feature maps sequences of glyphs to corresponding ligatures (GSUB lookup type 4). Ligatures with more components need to be stored ahead of those with fewer components in order to be found. The set of discretionary ligatures will vary by design and script.

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria.

*UI suggestion:* This feature should be off by default.

*Script/language sensitivity:* None.

*Feature interaction:* This feature may be used in combination with other substitution (GSUB) features, whose results it may override. See also 'clig'.

## Tag: 'dnom'

*Friendly name:* Denominators

*Registered by:* Adobe

*Function:* Replaces selected figures which follow a slash with denominator figures.

*Example:* In the string 11/17 selected by the user, the application turns the 17 into denominators when the user applies the fraction feature (['frac'](features_fj#frac)).

*Recommended implementation:* Glyphs for figures (digits) and related characters (grouping or decimal separators) are mapped to corresponding denominator glyphs in the font (GSUB lookup type 1).

*Application interface:* In recommended usage, this feature is applied automatically as part of the application’s implementation for the 'frac' feature. See the ['frac'](features_fj#frac) for details.

*UI suggestion:* In recommended usage, this feature is applied to sequences automatically by applications when the 'frac' feature is used, and direct user control is not required.

*Script/language sensitivity:* None.

*Feature interaction:* This feature supports implementation of the ['frac'](features_fj#frac) feature. It may be used in combination with other substitution (GSUB) features, whose results it may override.

## Tag: 'dtls'

*Friendly name:* Dotless Forms

*Registered by:* Microsoft

*Function:* This feature provides dotless forms for Math Alphanumeric characters, such as U+1D422 MATHEMATICAL BOLD SMALL I, U+1D423 MATHEMATICAL BOLD SMALL J, U+1D456 U+MATHEMATICAL ITALIC SMALL I, U+1D457 MATHEMATICAL ITALIC SMALL J, and so on. The dotless forms are to be used as base forms for placing mathematical accents over them.

*Example:* When adding a tilde to an i, the dotless form is substituted before attaching the tilde accent on top of it.

*Recommended implementation:* Single substitution (GSUB lookup type 1), for glyphs of all dotted characters.

*Application interface:* In recommended usage, this feature is used to trigger substitutions that are required for correct display of math formulas. It should be applied to individual glyphs in appropriate contexts under the control of a math layout handler. See the ['MATH' table](math#opentype-layout-tags-used-with-the-math-table) chapter for additional information.

*UI suggestion:* Control of the feature should not generally be exposed to the user.

*Script/language sensitivity:* Used for math formula layout.

*Feature interaction:* None.

## Tag: 'expt'

*Friendly name:* Expert Forms

*Registered by:* Adobe

*Function:* Like the [JIS78 Forms](features_fj#jp78) feature, this feature replaces standard forms in Japanese fonts with corresponding forms preferred by typographers. Although most of the JIS78 substitutions are included, the expert substitution goes on to handle many more characters.

*Example:* The user would invoke this feature to replace kanji character U+5516 with U+555E.

*Recommended implementation:* A lookup table for the 'expt' feature maps many default (JIS90) GIDs to corresponding alternates (GSUB lookup type 1).

*Application interface:* Discretionary feature: can be applied to glyph runs based on document markup, user control or other application criteria. Note that the effect of this feature is effectively equivalent to a change of character code. Besides the original character code, the application should store the character code for the new glyph.

*UI suggestion:* Applications may choose to have this feature active or inactive by default, depending on their target markets.

*Script/language sensitivity:* Applies only to Japanese.

*Feature interaction:* This feature is mutually exclusive to most other features, which should be turned off when it’s applied. Exceptions include the ['palt'](features_pt#palt), ['vpal'](features_uz#vpal), ['vert'](features_uz#vert) and ['vrt2'](features_uz#vrt2) features, which may be used in addition.
