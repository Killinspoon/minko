/*
Copyright (c) 2013 Aerys

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "minko/file/AssetLibrary.hpp"
#include "minko/file/SceneParser.hpp"
#include "minko/scene/Node.hpp"
#include "msgpack.hpp"
#include <stack>

using namespace minko;
using namespace minko::file;
using namespace minko::math;

static std::map<int8_t, SceneParser::ComponentReadFunction> _componentIdToReadFunction;

void
SceneParser::registerComponent(int8_t					componentId,
							   ComponentReadFunction	readFunction)
{
	_componentIdToReadFunction[componentId] = readFunction;
}

void
SceneParser::parse(const std::string&					filename,
				   const std::string&					resolvedFilename,
				   std::shared_ptr<Options>				options,
				   const std::vector<unsigned char>&	data,
				   std::shared_ptr<AssetLibrary>		assetLibrary)
{
	msgpack::object		deserialized;
	msgpack::zone		mempool;
	std::string			str = extractDependencies(assetLibrary, data, options);

	msgpack::unpack(str.data(), str.size(), NULL, &mempool, &deserialized);
	msgpack::type::tuple<std::vector<std::string>, std::vector<SerializedNode>> dst;
	deserialized.convert(&dst);

	assetLibrary->node(filename, parseNode(dst.a1, dst.a0, assetLibrary));

	complete()->execute(shared_from_this());
}

scene::Node::Ptr
SceneParser::parseNode(std::vector<SerializedNode>			nodePack, 
					   std::vector<std::string>				componentPack,
					   std::shared_ptr<file::AssetLibrary>	assetLibrary)
{
	std::shared_ptr<scene::Node>								root;
	std::stack<std::tuple<std::shared_ptr<scene::Node>, uint>>	nodeStack;
	std::map<int, std::vector<std::shared_ptr<scene::Node>>>	componentIdToNodes;

	for (uint i = 0; i < nodePack.size(); ++i)
	{
		scene::Node::Ptr	newNode			= scene::Node::create();
		uint				layouts			= nodePack[i].a1;
		uint				numChildren		= nodePack[i].a2;
		std::vector<uint>	componentsId	= nodePack[i].a3; 

		newNode->layouts(layouts);
		newNode->name(nodePack[i].a0);
		
		for (uint componentId : componentsId)
			componentIdToNodes[componentId].push_back(newNode);

		if (nodeStack.size() == 0)
			root = newNode;
		else
		{
			scene::Node::Ptr parent = std::get<0>(nodeStack.top());

			parent->addChild(newNode);
			std::get<1>(nodeStack.top())--;

			if (std::get<1>(nodeStack.top()) == 0)
				nodeStack.pop();
		}

		if (numChildren > 0)
			nodeStack.push(std::make_tuple(newNode, numChildren));
	}

	for (uint componentIndex = 0; componentIndex < componentPack.size(); ++componentIndex)
	{
		msgpack::zone	mempool;
		msgpack::object deseriliazedIndex;
		int8_t			dst;

		msgpack::unpack(componentPack[componentIndex].data() + componentPack[componentIndex].size() - 1, 1, NULL, &mempool, &deseriliazedIndex);
		deseriliazedIndex.convert(&dst);

		if (_componentIdToReadFunction.find(dst) != _componentIdToReadFunction.end())
		{
			std::shared_ptr<component::AbstractComponent> newComponent = _componentIdToReadFunction[dst](componentPack[componentIndex], assetLibrary, _dependencies);

			for (scene::Node::Ptr node : componentIdToNodes[componentIndex])
				node->addComponent(newComponent);
		}
	}

	return root;
}