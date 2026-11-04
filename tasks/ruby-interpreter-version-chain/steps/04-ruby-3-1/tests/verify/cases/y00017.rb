begin
  p ["a", "b", "a"].tally
rescue Exception => e
  p e.class
end
