# frozen_string_literal: true
begin
  s = String.new("abc"); s << "d"; p s
rescue Exception => e
  p e.class
end
