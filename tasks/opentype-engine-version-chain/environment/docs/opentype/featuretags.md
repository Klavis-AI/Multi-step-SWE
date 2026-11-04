
# Feature tags (OpenType 1.9.1) - Typography | Microsoft Learn

Features provide information about how to use the glyphs in a font to layout text for a script or language. For example, an Arabic font might have a feature for substituting initial glyph forms, and a Kanji font might have a feature for positioning glyphs vertically. All OpenType Layout features define data for glyph substitution, glyph positioning, or both.

Each OpenType Layout feature has a feature tag that identifies its typographic function and effects. By examining a feature’s tag, a text-processing client can determine what a feature does and decide whether to utilize it during text layout.

All tags are four-character strings composed of a limited set of ASCII characters; for details regarding the Tag data type, see [Data Types](otff#data-types). By convention, registered feature tags use four lowercase letters. For instance, the 'mark' feature is used in managing the placement of diacritical marks, and the 'swsh' feature is used to select alternate swash glyphs.

The tag space of tags consisting of four uppercase letters (A-Z) with no punctuation, spaces, or numbers, is reserved as a vendor space. Font vendors may use such tags to identify private features. For example, the feature tag PKRN might designate a private feature used to kern punctuation marks. Interoperability of private features is not guaranteed.

A feature definition might not provide all the information required to properly implement glyph substitution or positioning actions. In many cases, a text-processing client may need to supply additional data. For example, the function of the 'init' feature is to provide initial glyph forms. Nothing in the feature’s lookup tables indicates when or where to apply this feature during text processing. To correctly use the 'init' feature in Arabic text, in which initial glyph forms appear at the beginning of connected letter groups determined by character-joining properties, text-processing clients must be able to identify the glyphs to which the feature should be applied, based on character context and joining properties. In all cases, the text-processing client is responsible for applying, combining, and arbitrating among features and rendering the result.

The feature descriptions provided can include details regarding suggested lookup types that can be used to implement a feature. Some feature descriptions include feature-specific details regarding how text-layout software is expected to interact with the feature. General information on processing of lookup tables is provided in the ['GPOS'](gpos), ['GSUB'](gsub) and [OpenType Layout Common Table Formats](chapter2) chapters. Information provided in feature descriptions regarding lookups and lookup types is provided only as recommendations or suggestions; the set of lookups used to implement a feature may vary across platforms, applications, fonts, and font developers.

## Registered features

- [Features listed alphabetically](featurelist)
- [Feature descriptions and implementations](features_ae)

## How to register features

Microsoft encourages font developers to use “registered” feature tags when implementing registered features. However, font developers also may define and register their own features.

Microsoft welcomes nominations for new features and feature tags to register. To qualify for registration, a feature must have a single function that is clearly identified by its tag. The function of the feature should be defined at the lowest useful level and must be distinctly different from the functions of currently registered features. When font developers register feature tags and functions with Microsoft, they do not have to supply implementation details.

Microsoft reserves the right to officially assign feature tags in the Microsoft Tag Registry. Although Microsoft has reserved the feature and feature tag definitions listed in this registry, Microsoft fonts do not necessarily contain all of the features.
