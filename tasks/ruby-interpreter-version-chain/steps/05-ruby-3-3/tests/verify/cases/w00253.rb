begin
  p(({[1] => {c: 2}}).values)
rescue Exception => e
  p e.class
end
