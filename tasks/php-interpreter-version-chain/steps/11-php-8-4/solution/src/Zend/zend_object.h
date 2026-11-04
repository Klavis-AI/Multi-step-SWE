#ifndef ZEND_OBJECT_H
#define ZEND_OBJECT_H
#include "zend.h"
#include "zend_ast.h"

void zend_register_class(zend_class_entry *ce);
zend_property_info *zend_find_property_info(zend_class_entry *ce, zend_string *name);
int   zend_check_property_access(zend_property_info *info, zend_class_entry *scope);
int   zend_check_method_access(zend_function *fn, zend_class_entry *scope);
zval *zend_object_get_prop_slot(zend_object *obj, zend_string *name, int *inaccessible);
void  zend_class_init_statics(zend_class_entry *ce);
zend_class_entry *zend_declare_class(zend_class_entry *decl);   /* bind at run time */
zend_class_entry *zend_get_current_scope(void);
#endif
