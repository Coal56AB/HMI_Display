"""Live columns use sample time; history is transferred as a complete window."""
class GraphStream:
    def __init__(self):
        self.key = None
        self.bucket = None

    def columns(self, graph, budget=240):
        key = (graph['page'], graph.get('revision'), graph['division'])
        end = graph['bucket']
        first = max(0, end - 239)
        if key == self.key and self.bucket is not None and end >= self.bucket:
            first = max(first, self.bucket + 1)
        self.key, self.bucket = key, end
        if first > end:
            return []
        # A large gap is cheaper as a current frame than hundreds of ACKs.
        if end - first + 1 > budget:
            return None
        def column(at):
            index = at % 240
            values = [v[index] for v in graph['channels']]
            return (graph['page'], index + 1, values, graph['ranges'], graph['scales'])
        return [column(at) for at in range(first, end + 1)]
