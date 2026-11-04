begin
  p((1..4).tally)
rescue Exception => e
  p e.class
end
