def f
  return 1
ensure
  $e = :ran
end
p f
p $e
