# frozen_string_literal: false
begin
  p "abc".equal?("abc")
rescue Exception => e
  p e.class
end
