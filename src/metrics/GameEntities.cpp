#include <demo.hpp>

int		readFieldIndex(standardParameters, bool newWay, int oldindex)
{
	if (newWay && readBits(1))
	{
		return oldindex + 1;
	}
	int rval = 0;
	if (newWay && readBits(1))
		rval = readBits(3);
	else
	{
		rval = readBits(7);
		switch (rval & (32 | 64))
		{
			case 32:
				rval = (rval & ~96) | (readBits(2) << 5);
				break;
			case 64:
				rval = (rval & ~96) | (readBits(4) << 5);
				break;
			case 96:
				rval = (rval & ~96) | (readBits(7) << 5);
				break;
			default:
				break;
		}
	}
	if (rval == 0xFFF)
		return -1;
	return oldindex + 1 + rval;
}

void	decodeProperty(standardParameters, int &ind, const DataTable &dt, GameEntities::Entity &ent, const PropW *arProp = 0)
{
#define string std::string
#define DecodeSwitch(i, typeV) \
	case i: \
	{ \
		typeV rv = decode##typeV(standardIParameters, flatProp.prop); \
		printIfAllowed("--entitymsg", std::cout << flatProp.path << " : " << rv << std::endl); \
		GameEntities::Property prop; \
		prop.name = flatProp.path; \
		prop.type = decoded_##typeV; \
		prop.data = new typeV(rv); \
		if (ind >= ent.properties.size()) \
			ErrorReturnMessage("kanker"); \
		ent.properties[ind] = prop; \
		break; \
	}
	assert(ind < ent.parentService.props.size());

	if (!arProp)
		arProp = &ent.parentService.props[ind];
	const PropW &flatProp = *arProp;

	switch (flatProp.prop.type())
	{
		DecodeSwitch(0, int);
		DecodeSwitch(1, float);
		DecodeSwitch(2, Vector);
		DecodeSwitch(3, Vector2);
		DecodeSwitch(4, string);
		case 5:
		{
			int maxElem = flatProp.prop.num_elements();
			int bitsToRead = 1;
			while (maxElem >>= 1)
				bitsToRead++;
			int numElem = readBits(bitsToRead);
			for (size_t x = 0; x < numElem; x++)
			{
				PropW newProp = PropW(flatProp.targetElem, flatProp.path + '.' + std::to_string(x));
				decodeProperty(standardIParameters, ind, dt, ent, &newProp);
			}
			break;
		}
	
	default:
		{
			ErrorReturnMessage("Error case not found! type: " + std::to_string(flatProp.prop.type()));
		}
	}
#undef string
#undef DecodeSwitch
}

void	readFromStream(standardParameters, const DataTable &dt, GameEntities::Entity &ent)
{
	bool readNewWay = readBits(1) == 1 ? true : false;

	std::vector<int> indicies;
	int index = -1;
	
	while ((index = readFieldIndex(standardIParameters, readNewWay, index)) != -1)
		indicies.push_back(index);

	for (size_t x = 0; x < indicies.size(); x++)
	{
		decodeProperty(standardIParameters, indicies[x], dt, ent);
	}
}

DataTable::ServiceClass	PVSParser(standardParameters, int &id, DataTable &dt)
{
	int serverClassId = readBits(dt.serviceClassBits);

	int serial = readBits(10);
	
	DataTable::ServiceClass nSC = DataTable::ServiceClass(dt.services[serverClassId]);

	return nSC;
}

void GameEntities::parse(PacketEntities &pe, DataTable &dt)
{
	const std::string &data = pe.entity_data();
	int i = 0;
	char bitsAvailable = 8;
	int currentEntity = -1;

	staged.clear();

	size_t x = 0;
	for (; x < pe.updated_entries(); x++)
	{
		StagedChange newChange;

		currentEntity += 1 + readStringVarInt(standardIParameters);

		newChange.index = currentEntity;
		printIfAllowed("--entitymsg", std::cout << "-------[Current Entity: " << currentEntity << ", bytes read: << " << i << "]" << std::endl);
		if (readBits(1) == 0)
		{
			if (readBits(1))
			{
				newChange.type = 0;

				newChange.data.parentService = PVSParser(standardIParameters, currentEntity, dt);
				newChange.data.properties.resize(newChange.data.parentService.props.size());

				printIfAllowed("--entitymsg", std::cout << "Create" << std::endl);
				printIfAllowed("--entitymsg", std::cout << newChange.data.parentService << std::endl);
				readFromStream(standardIParameters, dt, newChange.data);
			}
			else
			{
				newChange.type = 1;
				newChange.data.parentService = props[currentEntity].parentService;
				newChange.data.properties.resize(newChange.data.parentService.props.size());

				printIfAllowed("--entitymsg", std::cout << "Update" << std::endl);
				printIfAllowed("--entitymsg", std::cout << newChange.data.parentService << std::endl);
				readFromStream(standardIParameters, dt, newChange.data);
			}
		}
		else
		{
			newChange.type = 2;

			printIfAllowed("--entitymsg", std::cout << "Delete" << std::endl);
			printIfAllowed("--entitymsg", std::cout << props[currentEntity].parentService << std::endl);

			DataTable::ServiceClass nullified = DataTable::ServiceClass();
			nullified.id = -1;

			readBits(1);
		}

		staged.push_back(newChange);

		assert(i < data.length());
	}
	assert(x == pe.updated_entries());
	// assert(i == data.length()); this fails, dont know if its a big deal
}

const std::vector<GameEntities::StagedChange>	&GameEntities::getStagedChanges()
{
	return staged;
}

void		GameEntities::executeChanges()
{
	for (size_t i = 0; i < staged.size(); i++)
	{
		if (staged[i].type == 0)
		{
			if (props.size() <= staged[i].index)
				props.resize(staged[i].index * 2 == 0 ? 1 : staged[i].index * 2);
			props[staged[i].index] = staged[i].data;
		}
		else if (staged[i].type == 1)
		{
			Entity	&ref = props[staged[i].index];
			for (size_t x = 0; x < staged[i].data.properties.size(); x++)
			{
				if (staged[i].data.properties[x].name.length())
				{
					ref.properties[x] = staged[i].data.properties[x];
				}
			}
		}
		else if (staged[i].type == 2)
		{
			Entity nullified;

			props[staged[i].index] = nullified;
		}
	}
	staged.clear();
}

GameEntities::GameEntities() {}

GameEntities::Property::~Property()
{
	// if (data)
	// 	delete (char *)data;
}


/*
	kanker
*/

GameEntities::StagedChange	&GameEntities::StagedChange::operator=(const GameEntities::StagedChange &s)
{
	type = s.type;
	index = s.index;
	data = s.data;

	return (*this);
}
GameEntities::Entity		&GameEntities::Entity::operator=(const GameEntities::Entity &s)
{
	parentService = s.parentService;
	properties = s.properties;

	return (*this);
}
GameEntities::Property		&GameEntities::Property::operator=(const GameEntities::Property &s)
{
	name = s.name;
	type = s.type;
	data = s.data;

	return (*this);
}