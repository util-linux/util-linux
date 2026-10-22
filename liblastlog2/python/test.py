import pylastlog2

def test_query():
  result = pylastlog2.query('root')
  for entry in ['user', 'time', 'tty', 'rhost', 'service']:
    assert entry in result
  assert isinstance(result.get('time'), int)

test_query()
