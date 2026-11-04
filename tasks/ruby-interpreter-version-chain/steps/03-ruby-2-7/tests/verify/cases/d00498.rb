# frozen_string_literal: true
begin
  p(-"abc").frozen?
rescue Exception => e
  p e.class
end
