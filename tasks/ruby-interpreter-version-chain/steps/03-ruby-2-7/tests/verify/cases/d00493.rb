# frozen_string_literal: true
begin
  s = "abc"; s.upcase!; p s
rescue Exception => e
  p e.class
end
