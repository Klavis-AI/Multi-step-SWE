# -*- coding: us-ascii -*-
# frozen_string_literal: false
FOO = "foo"
p(//.class)
    p(!!('a' =~ //))
    p(!!('' =~ //))
    p(/a/.class)
    p(!!('a' =~ /a/))
    assert_no_match(/test/, 'tes')
    re = /test/
    p(!!('test' =~ re))
    str = 'test'
    p(!!(str =~ re))
    p(!!(str =~ /test/))
    p((/test/ =~ 'test'))
    p((re =~ 'test'))
    p((/test/ =~ str))
    p((re =~ str))
    p(('test' =~ /test/))
    p(('test' =~ re))
    p((str =~ /test/))
    p((str =~ re))