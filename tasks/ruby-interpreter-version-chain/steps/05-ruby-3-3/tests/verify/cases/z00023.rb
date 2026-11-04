begin
  e = RuntimeError.new("boom")
  p e.detailed_message(highlight: false)
rescue Exception => e
  p e.class
end
