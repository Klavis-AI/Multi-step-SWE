begin
  p(({[1] => {c: 2}}).invert)
rescue Exception => e
  p e.class
end
