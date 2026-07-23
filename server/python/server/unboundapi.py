import os
import json

try:
    import requests
except ImportError:
    pass


def update_client_data(user_id: str, data: dict) -> bool:
    api_server = os.environ.get("API_SERVER", "http://localhost:8888")

    try:
        requests.post("{0}/update".format(api_server), data={
            "user_id": user_id,
            "data": json.dumps(data)
        })
    except requests.ConnectionError as e:
        return False
    except requests.HTTPError as e:
        return False
    else:
        return True
