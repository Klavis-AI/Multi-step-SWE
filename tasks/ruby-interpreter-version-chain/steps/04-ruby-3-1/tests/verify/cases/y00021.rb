begin
  p "abc".match?(/b/)
  p "abc".match?(/z/)
rescue Exception => e
  p e.class
end
