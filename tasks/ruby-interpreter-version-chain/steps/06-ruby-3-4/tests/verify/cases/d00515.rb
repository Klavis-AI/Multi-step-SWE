# frozen_string_literal: false
begin
  s = String.new("abc"); s << "d"; p s
rescue Exception => e
  p e.class
end
