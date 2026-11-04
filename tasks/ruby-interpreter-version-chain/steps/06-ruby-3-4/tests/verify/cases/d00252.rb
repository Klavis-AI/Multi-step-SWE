begin
  p("ab".append_as_bytes("c"))
rescue Exception => e
  p e.class
end
