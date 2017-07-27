#include "Nodes/Models/FVTXNode.h"

FVTXNode::FVTXNode(const FVTX& fvtx, QObject* parent) : Node(parent), m_fvtx(fvtx)
{
}

CustomStandardItem* FVTXNode::MakeItem()
{
  if (!m_header_loaded)
  {
    ResultCode res = m_fvtx.ReadHeader();
    if (res != ResultCode::Success)
    {
      emit NewStatus(res);
      return nullptr;
    }
    else
      m_fvtx_header = m_fvtx.GetHeader();
  }
  CustomStandardItem* item = new CustomStandardItem;
  item->setData("FVTX " + QString::number(m_fvtx_header.section_index), Qt::DisplayRole);
  item->setData(QVariant::fromValue<Node*>(static_cast<Node*>(this)), Qt::UserRole + 1);
  return item;
}

ResultCode FVTXNode::LoadAttributeArea()
{
  if (!m_header_loaded)
  {
    ResultCode res = m_fvtx.ReadHeader();
    if (res != ResultCode::Success)
    {
      emit NewStatus(res);
      return ResultCode::Success;
    }
    else
      m_fvtx_header = m_fvtx.GetHeader();
  }

  quint32 row = 0;
  QStandardItemModel* header_attributes_model = new QStandardItemModel;
  m_delegate_group = CustomItemDelegate::DelegateGroup();

  // Magic
  header_attributes_model->setItem(row, 0, new QStandardItem("Magic File Identifier"));
  CustomStandardItem* magic_item = new CustomStandardItem(m_fvtx_header.magic);
  magic_item->SetFunction([this](QString text) { m_fvtx_header.magic = text; });
  header_attributes_model->setItem(row, 1, magic_item);
  m_delegate_group.line_edit_delegates << row;
  ++row;

  // Attribute Count
  header_attributes_model->setItem(row, 0, new QStandardItem("Attribute Count"));
  CustomStandardItem* attribute_count_item =
      new CustomStandardItem(QString::number(m_fvtx_header.attribute_count));
  // TODO: Attribute count is actually a char, and not a short.
  attribute_count_item->SetFunction(
      [this](QString text) { m_fvtx_header.attribute_count = text.toUShort(); });
  header_attributes_model->setItem(row, 1, attribute_count_item);
  m_delegate_group.spin_box_delegates << row;
  ++row;

  // Buffer Count
  header_attributes_model->setItem(row, 0, new QStandardItem("Buffer Count"));
  CustomStandardItem* buffer_count_item =
      new CustomStandardItem(QString::number(m_fvtx_header.buffer_count));
  // TODO: This is a char.
  buffer_count_item->SetFunction(
      [this](QString text) { m_fvtx_header.buffer_count = text.toUShort(); });
  header_attributes_model->setItem(row, 1, buffer_count_item);
  m_delegate_group.spin_box_delegates << row;
  ++row;

  // Section Index
  header_attributes_model->setItem(row, 0, new QStandardItem("Array Index"));
  CustomStandardItem* array_index_item =
      new CustomStandardItem(QString::number(m_fvtx_header.section_index));
  array_index_item->SetFunction(
      [this](QString text) { m_fvtx_header.section_index = text.toUShort(); });
  header_attributes_model->setItem(row, 1, array_index_item);
  m_delegate_group.spin_box_delegates << row;
  ++row;

  // Number of Verticies
  header_attributes_model->setItem(row, 0, new QStandardItem("Number Of Vertices"));
  CustomStandardItem* num_vertices_item =
      new CustomStandardItem(QString::number(m_fvtx_header.number_vertices));
  num_vertices_item->SetFunction(
      [this](QString text) { m_fvtx_header.number_vertices = text.toUInt(); });
  header_attributes_model->setItem(row, 1, num_vertices_item);
  m_delegate_group.spin_box_delegates << row;
  ++row;

  // Vertex Skin Count
  header_attributes_model->setItem(row, 0, new QStandardItem("Skin Cont"));
  CustomStandardItem* skin_count_item =
      new CustomStandardItem(QString::number(m_fvtx_header.vertex_skin_count));
  // TODO: This is a char.
  skin_count_item->SetFunction(
      [this](QString text) { m_fvtx_header.vertex_skin_count = text.toUShort(); });
  header_attributes_model->setItem(row, 1, skin_count_item);
  m_delegate_group.spin_box_delegates << row;
  ++row;

  // Attribute Array Offset
  header_attributes_model->setItem(row, 0, new QStandardItem("Attribute Array Offset"));
  header_attributes_model->setItem(
      row, 1, new QStandardItem("0x" + QString::number(m_fvtx_header.attribute_array_offset, 16)));
  ++row;

  // Attribute Index Group Offset
  header_attributes_model->setItem(row, 0, new QStandardItem("Attribute Index Group Offset"));
  header_attributes_model->setItem(
      row, 1,
      new QStandardItem("0x" + QString::number(m_fvtx_header.attribute_index_group_offset, 16)));
  ++row;

  // Buffer Array Offset
  header_attributes_model->setItem(row, 0, new QStandardItem("Buffer Array Offset"));
  header_attributes_model->setItem(
      row, 1, new QStandardItem("0x" + QString::number(m_fvtx_header.buffer_array_offset, 16)));
  ++row;

  // User Runtime Pointer
  header_attributes_model->setItem(row, 0, new QStandardItem("User Runtime Pointer"));
  header_attributes_model->setItem(
      row, 1, new QStandardItem("0x" + QString::number(m_fvtx_header.user_pointer_runtime, 16)));
  ++row;

  header_attributes_model->setRowCount(row);
  header_attributes_model->setColumnCount(2);

  connect(header_attributes_model, &QStandardItemModel::itemChanged, this,
          &FVTXNode::HandleAttributeItemChange);

  emit NewAttributesArea(MakeAttributeSection(header_attributes_model));
  return ResultCode::Success;
}

void FVTXNode::HandleAttributeItemChange(QStandardItem* item)
{
  // Colum 1 is the only editable row
  if (item->column() != 1)
    return;

  CustomStandardItem* custom_item = dynamic_cast<CustomStandardItem*>(item);
  if (custom_item)
    custom_item->ExecuteFunction();

  m_fvtx.SetHeader(m_fvtx_header);
}