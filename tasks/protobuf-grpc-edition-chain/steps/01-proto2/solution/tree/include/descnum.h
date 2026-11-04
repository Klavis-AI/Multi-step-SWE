/* descnum.h -- field numbers of descriptor.proto. GENERATED, do not edit.
 * Regenerate with: tools/gen_descnum.py wkt/google/protobuf/descriptor.proto include/descnum.h
 */
#ifndef PB_DESCNUM_H
#define PB_DESCNUM_H

/* FileDescriptorSet */
#define F_FILEDESCRIPTORSET_FILE                         1    /* FileDescriptorProto */

/* FileDescriptorProto */
#define F_FILEDESCRIPTORPROTO_NAME                         1    /* string */
#define F_FILEDESCRIPTORPROTO_PACKAGE                      2    /* string */
#define F_FILEDESCRIPTORPROTO_DEPENDENCY                   3    /* string */
#define F_FILEDESCRIPTORPROTO_PUBLIC_DEPENDENCY            10   /* int32 */
#define F_FILEDESCRIPTORPROTO_WEAK_DEPENDENCY              11   /* int32 */
#define F_FILEDESCRIPTORPROTO_OPTION_DEPENDENCY            15   /* string */
#define F_FILEDESCRIPTORPROTO_MESSAGE_TYPE                 4    /* DescriptorProto */
#define F_FILEDESCRIPTORPROTO_ENUM_TYPE                    5    /* EnumDescriptorProto */
#define F_FILEDESCRIPTORPROTO_SERVICE                      6    /* ServiceDescriptorProto */
#define F_FILEDESCRIPTORPROTO_EXTENSION                    7    /* FieldDescriptorProto */
#define F_FILEDESCRIPTORPROTO_OPTIONS                      8    /* FileOptions */
#define F_FILEDESCRIPTORPROTO_SOURCE_CODE_INFO             9    /* SourceCodeInfo */
#define F_FILEDESCRIPTORPROTO_SYNTAX                       12   /* string */
#define F_FILEDESCRIPTORPROTO_EDITION                      14   /* Edition */

/* DescriptorProto */
#define F_DESCRIPTORPROTO_NAME                         1    /* string */
#define F_DESCRIPTORPROTO_FIELD                        2    /* FieldDescriptorProto */
#define F_DESCRIPTORPROTO_EXTENSION                    6    /* FieldDescriptorProto */
#define F_DESCRIPTORPROTO_NESTED_TYPE                  3    /* DescriptorProto */
#define F_DESCRIPTORPROTO_ENUM_TYPE                    4    /* EnumDescriptorProto */
#define F_DESCRIPTORPROTO_EXTENSION_RANGE              5    /* ExtensionRange */
#define F_DESCRIPTORPROTO_ONEOF_DECL                   8    /* OneofDescriptorProto */
#define F_DESCRIPTORPROTO_OPTIONS                      7    /* MessageOptions */
#define F_DESCRIPTORPROTO_RESERVED_RANGE               9    /* ReservedRange */
#define F_DESCRIPTORPROTO_RESERVED_NAME                10   /* string */
#define F_DESCRIPTORPROTO_VISIBILITY                   11   /* SymbolVisibility */

/* DescriptorProto.ExtensionRange */
#define F_DESCRIPTORPROTO_EXTENSIONRANGE_START                        1    /* int32 */
#define F_DESCRIPTORPROTO_EXTENSIONRANGE_END                          2    /* int32 */
#define F_DESCRIPTORPROTO_EXTENSIONRANGE_OPTIONS                      3    /* ExtensionRangeOptions */

/* DescriptorProto.ReservedRange */
#define F_DESCRIPTORPROTO_RESERVEDRANGE_START                        1    /* int32 */
#define F_DESCRIPTORPROTO_RESERVEDRANGE_END                          2    /* int32 */

/* FieldDescriptorProto */
#define F_FIELDDESCRIPTORPROTO_NAME                         1    /* string */
#define F_FIELDDESCRIPTORPROTO_NUMBER                       3    /* int32 */
#define F_FIELDDESCRIPTORPROTO_LABEL                        4    /* Label */
#define F_FIELDDESCRIPTORPROTO_TYPE                         5    /* Type */
#define F_FIELDDESCRIPTORPROTO_TYPE_NAME                    6    /* string */
#define F_FIELDDESCRIPTORPROTO_EXTENDEE                     2    /* string */
#define F_FIELDDESCRIPTORPROTO_DEFAULT_VALUE                7    /* string */
#define F_FIELDDESCRIPTORPROTO_ONEOF_INDEX                  9    /* int32 */
#define F_FIELDDESCRIPTORPROTO_JSON_NAME                    10   /* string */
#define F_FIELDDESCRIPTORPROTO_OPTIONS                      8    /* FieldOptions */
#define F_FIELDDESCRIPTORPROTO_PROTO3_OPTIONAL              17   /* bool */

/* OneofDescriptorProto */
#define F_ONEOFDESCRIPTORPROTO_NAME                         1    /* string */
#define F_ONEOFDESCRIPTORPROTO_OPTIONS                      2    /* OneofOptions */

/* EnumDescriptorProto */
#define F_ENUMDESCRIPTORPROTO_NAME                         1    /* string */
#define F_ENUMDESCRIPTORPROTO_VALUE                        2    /* EnumValueDescriptorProto */
#define F_ENUMDESCRIPTORPROTO_OPTIONS                      3    /* EnumOptions */
#define F_ENUMDESCRIPTORPROTO_RESERVED_RANGE               4    /* EnumReservedRange */
#define F_ENUMDESCRIPTORPROTO_RESERVED_NAME                5    /* string */
#define F_ENUMDESCRIPTORPROTO_VISIBILITY                   6    /* SymbolVisibility */

/* EnumDescriptorProto.EnumReservedRange */
#define F_ENUMDESCRIPTORPROTO_ENUMRESERVEDRANGE_START                        1    /* int32 */
#define F_ENUMDESCRIPTORPROTO_ENUMRESERVEDRANGE_END                          2    /* int32 */

/* EnumValueDescriptorProto */
#define F_ENUMVALUEDESCRIPTORPROTO_NAME                         1    /* string */
#define F_ENUMVALUEDESCRIPTORPROTO_NUMBER                       2    /* int32 */
#define F_ENUMVALUEDESCRIPTORPROTO_OPTIONS                      3    /* EnumValueOptions */

/* ServiceDescriptorProto */
#define F_SERVICEDESCRIPTORPROTO_NAME                         1    /* string */
#define F_SERVICEDESCRIPTORPROTO_METHOD                       2    /* MethodDescriptorProto */
#define F_SERVICEDESCRIPTORPROTO_OPTIONS                      3    /* ServiceOptions */

/* MethodDescriptorProto */
#define F_METHODDESCRIPTORPROTO_NAME                         1    /* string */
#define F_METHODDESCRIPTORPROTO_INPUT_TYPE                   2    /* string */
#define F_METHODDESCRIPTORPROTO_OUTPUT_TYPE                  3    /* string */
#define F_METHODDESCRIPTORPROTO_OPTIONS                      4    /* MethodOptions */
#define F_METHODDESCRIPTORPROTO_CLIENT_STREAMING             5    /* bool */
#define F_METHODDESCRIPTORPROTO_SERVER_STREAMING             6    /* bool */

/* FileOptions */
#define F_FILEOPTIONS_JAVA_PACKAGE                 1    /* string */
#define F_FILEOPTIONS_JAVA_OUTER_CLASSNAME         8    /* string */
#define F_FILEOPTIONS_JAVA_MULTIPLE_FILES          10   /* bool */
#define F_FILEOPTIONS_JAVA_GENERATE_EQUALS_AND_HASH 20   /* bool */
#define F_FILEOPTIONS_JAVA_STRING_CHECK_UTF8       27   /* bool */
#define F_FILEOPTIONS_OPTIMIZE_FOR                 9    /* OptimizeMode */
#define F_FILEOPTIONS_GO_PACKAGE                   11   /* string */
#define F_FILEOPTIONS_CC_GENERIC_SERVICES          16   /* bool */
#define F_FILEOPTIONS_JAVA_GENERIC_SERVICES        17   /* bool */
#define F_FILEOPTIONS_PY_GENERIC_SERVICES          18   /* bool */
#define F_FILEOPTIONS_DEPRECATED                   23   /* bool */
#define F_FILEOPTIONS_CC_ENABLE_ARENAS             31   /* bool */
#define F_FILEOPTIONS_OBJC_CLASS_PREFIX            36   /* string */
#define F_FILEOPTIONS_CSHARP_NAMESPACE             37   /* string */
#define F_FILEOPTIONS_SWIFT_PREFIX                 39   /* string */
#define F_FILEOPTIONS_PHP_CLASS_PREFIX             40   /* string */
#define F_FILEOPTIONS_PHP_NAMESPACE                41   /* string */
#define F_FILEOPTIONS_PHP_METADATA_NAMESPACE       44   /* string */
#define F_FILEOPTIONS_RUBY_PACKAGE                 45   /* string */
#define F_FILEOPTIONS_FEATURES                     50   /* FeatureSet */
#define F_FILEOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* MessageOptions */
#define F_MESSAGEOPTIONS_MESSAGE_SET_WIRE_FORMAT      1    /* bool */
#define F_MESSAGEOPTIONS_NO_STANDARD_DESCRIPTOR_ACCESSOR 2    /* bool */
#define F_MESSAGEOPTIONS_DEPRECATED                   3    /* bool */
#define F_MESSAGEOPTIONS_MAP_ENTRY                    7    /* bool */
#define F_MESSAGEOPTIONS_DEPRECATED_LEGACY_JSON_FIELD_CONFLICTS 11   /* bool */
#define F_MESSAGEOPTIONS_FEATURES                     12   /* FeatureSet */
#define F_MESSAGEOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* FieldOptions */
#define F_FIELDOPTIONS_CTYPE                        1    /* CType */
#define F_FIELDOPTIONS_PACKED                       2    /* bool */
#define F_FIELDOPTIONS_JSTYPE                       6    /* JSType */
#define F_FIELDOPTIONS_LAZY                         5    /* bool */
#define F_FIELDOPTIONS_UNVERIFIED_LAZY              15   /* bool */
#define F_FIELDOPTIONS_DEPRECATED                   3    /* bool */
#define F_FIELDOPTIONS_WEAK                         10   /* bool */
#define F_FIELDOPTIONS_DEBUG_REDACT                 16   /* bool */
#define F_FIELDOPTIONS_RETENTION                    17   /* OptionRetention */
#define F_FIELDOPTIONS_TARGETS                      19   /* OptionTargetType */
#define F_FIELDOPTIONS_EDITION_DEFAULTS             20   /* EditionDefault */
#define F_FIELDOPTIONS_FEATURES                     21   /* FeatureSet */
#define F_FIELDOPTIONS_FEATURE_SUPPORT              22   /* FeatureSupport */
#define F_FIELDOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* OneofOptions */
#define F_ONEOFOPTIONS_FEATURES                     1    /* FeatureSet */
#define F_ONEOFOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* EnumOptions */
#define F_ENUMOPTIONS_ALLOW_ALIAS                  2    /* bool */
#define F_ENUMOPTIONS_DEPRECATED                   3    /* bool */
#define F_ENUMOPTIONS_DEPRECATED_LEGACY_JSON_FIELD_CONFLICTS 6    /* bool */
#define F_ENUMOPTIONS_FEATURES                     7    /* FeatureSet */
#define F_ENUMOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* EnumValueOptions */
#define F_ENUMVALUEOPTIONS_DEPRECATED                   1    /* bool */
#define F_ENUMVALUEOPTIONS_FEATURES                     2    /* FeatureSet */
#define F_ENUMVALUEOPTIONS_DEBUG_REDACT                 3    /* bool */
#define F_ENUMVALUEOPTIONS_FEATURE_SUPPORT              4    /* FieldOptions.FeatureSupport */
#define F_ENUMVALUEOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* ServiceOptions */
#define F_SERVICEOPTIONS_FEATURES                     34   /* FeatureSet */
#define F_SERVICEOPTIONS_DEPRECATED                   33   /* bool */
#define F_SERVICEOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* MethodOptions */
#define F_METHODOPTIONS_DEPRECATED                   33   /* bool */
#define F_METHODOPTIONS_IDEMPOTENCY_LEVEL            34   /* IdempotencyLevel */
#define F_METHODOPTIONS_FEATURES                     35   /* FeatureSet */
#define F_METHODOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */

/* SourceCodeInfo */
#define F_SOURCECODEINFO_LOCATION                     1    /* Location */

/* SourceCodeInfo.Location */
#define F_SOURCECODEINFO_LOCATION_PATH                         1    /* int32 */
#define F_SOURCECODEINFO_LOCATION_SPAN                         2    /* int32 */
#define F_SOURCECODEINFO_LOCATION_LEADING_COMMENTS             3    /* string */
#define F_SOURCECODEINFO_LOCATION_TRAILING_COMMENTS            4    /* string */
#define F_SOURCECODEINFO_LOCATION_LEADING_DETACHED_COMMENTS    6    /* string */

/* UninterpretedOption */
#define F_UNINTERPRETEDOPTION_NAME                         2    /* NamePart */
#define F_UNINTERPRETEDOPTION_IDENTIFIER_VALUE             3    /* string */
#define F_UNINTERPRETEDOPTION_POSITIVE_INT_VALUE           4    /* uint64 */
#define F_UNINTERPRETEDOPTION_NEGATIVE_INT_VALUE           5    /* int64 */
#define F_UNINTERPRETEDOPTION_DOUBLE_VALUE                 6    /* double */
#define F_UNINTERPRETEDOPTION_STRING_VALUE                 7    /* bytes */
#define F_UNINTERPRETEDOPTION_AGGREGATE_VALUE              8    /* string */

/* UninterpretedOption.NamePart */
#define F_UNINTERPRETEDOPTION_NAMEPART_NAME_PART                    1    /* string */
#define F_UNINTERPRETEDOPTION_NAMEPART_IS_EXTENSION                 2    /* bool */

/* FeatureSet */
#define F_FEATURESET_FIELD_PRESENCE               1    /* FieldPresence */

/* ExtensionRangeOptions */
#define F_EXTENSIONRANGEOPTIONS_UNINTERPRETED_OPTION         999  /* UninterpretedOption */
#define F_EXTENSIONRANGEOPTIONS_DECLARATION                  2    /* Declaration */
#define F_EXTENSIONRANGEOPTIONS_FEATURES                     50   /* FeatureSet */
#define F_EXTENSIONRANGEOPTIONS_VERIFICATION                 3    /* VerificationState */

#endif /* PB_DESCNUM_H */
